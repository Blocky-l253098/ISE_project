#pragma once

// =============================================================================
// Screens.h - everything the cashier sees
//   Ui                          small helpers that build labels, boxes, buttons
//   StatusForm                  base form with a status line
//   ProductText / BillText      domain object -> text for the screen
//   BarcodeForm, ProductForm, BillForm, QuantityForm, HeldOrdersForm, OrderForm
//   IScreenFactory, MenuForm    the main menu only knows the factory interface
//   Depends on                  interfaces of Domain.h / Services.h, Camera.h controls;
//                               no file classes and no PosFactory
// =============================================================================

#include "Common.h"
#include "Barcode.h"
#include "Domain.h"
#include "Services.h"
#include "Camera.h"

// ---------------------------------------------------------------------------
// Small helpers that build controls (one place for positions, fonts, handlers)
// ---------------------------------------------------------------------------
public ref class Ui abstract sealed
{
public:
    static Label^ MakeLabel(String^ text, int x, int y)
    {
        Label^ label = gcnew Label();
        label->Text = text;
        label->Location = Point(x, y);
        label->AutoSize = true;
        return label;
    }

    static TextBox^ MakeTextBox(int x, int y, int width)
    {
        TextBox^ box = gcnew TextBox();
        box->Location = Point(x, y);
        box->Size = Drawing::Size(width, 23);
        return box;
    }

    // Read-only multi-line box with a fixed-width font (descriptions, bills)
    static TextBox^ MakeViewer(int x, int y, int width, int height)
    {
        TextBox^ box = gcnew TextBox();
        box->Location = Point(x, y);
        box->Size = Drawing::Size(width, height);
        box->Multiline = true;
        box->ReadOnly = true;
        box->ScrollBars = ScrollBars::Vertical;
        box->Font = gcnew Drawing::Font("Consolas", 10.0f);
        return box;
    }

    static Button^ MakeButton(String^ text, int x, int y, int width, int height, EventHandler^ onClick)
    {
        Button^ button = gcnew Button();
        button->Text = text;
        button->Location = Point(x, y);
        button->Size = Drawing::Size(width, height);
        if (onClick != nullptr)
            button->Click += onClick;
        return button;
    }

    static void Warn(String^ title, String^ message)
    {
        MessageBox::Show(message, title, MessageBoxButtons::OK, MessageBoxIcon::Warning);
    }

    static void Info(String^ title, String^ message)
    {
        MessageBox::Show(message, title, MessageBoxButtons::OK, MessageBoxIcon::Information);
    }
};

// Base of the screens that show a status line at the bottom
public ref class StatusForm : public Form
{
private:
    StatusStrip^ strip;

protected:
    ToolStripStatusLabel^ lblStatus;

    void SetStatus(String^ msg)
    {
        lblStatus->Text = msg;
    }

public:
    StatusForm(String^ firstMessage)
    {
        strip = gcnew StatusStrip();
        lblStatus = gcnew ToolStripStatusLabel(firstMessage);
        strip->Items->Add(lblStatus);
        this->Controls->Add(strip);
    }
};

// ---------------------------------------------------------------------------
// Domain object -> text for the screen
// ---------------------------------------------------------------------------
public ref class ProductText abstract sealed
{
public:
    static String^ Describe(Product^ product)
    {
        StringBuilder^ sb = gcnew StringBuilder();
        sb->AppendLine("Product ID   : " + product->Id.ToString());
        sb->AppendLine("Barcode      : " + product->Barcode);
        sb->AppendLine("Acronym      : " + product->Acronym);
        sb->AppendLine("Name         : " + product->Name);
        sb->AppendLine("Unit type    : " + product->Unit);
        sb->AppendLine("Unit price   : PKR " + Fmt::Money(product->Price));
        sb->AppendLine("Tax          : PKR " + Fmt::Money(product->Tax));
        sb->AppendLine("Discount     : PKR " + Fmt::Money(product->Discount));
        sb->AppendLine("Available    : " + Fmt::Number(product->Quantity));
        sb->AppendLine("Barcode file : " + product->BarcodeFile);
        return sb->ToString();
    }
};

public ref class BillText abstract sealed
{
public:
    static String^ Render(Bill^ bill)
    {
        Cart^ cart = bill->Lines;

        StringBuilder^ sb = gcnew StringBuilder();
        sb->AppendLine("BILL " + bill->Id);
        sb->AppendLine("Order    : " + bill->Source->Id + "  " + bill->Source->Date + " " + bill->Source->Time);
        sb->AppendLine("Customer : " + bill->Source->CustomerName);
        sb->AppendLine("Payment  : " + bill->Source->PaymentMode + " " + Fmt::Pack(bill->Source->Merchant));
        sb->AppendLine("Status   : " + bill->Source->Status);
        sb->AppendLine("----------------------------------------------");
        sb->AppendLine(Fmt::Left("Item", 10) + Fmt::Right("Qty", 6) + Fmt::Right("Price", 10) +
            Fmt::Right("Disc", 8) + Fmt::Right("Total", 10));

        for each(OrderItem ^ line in cart->Items)
        {
            sb->AppendLine(Fmt::Left(line->Item->Acronym, 10) +
                Fmt::Right(Fmt::Number(line->Quantity), 6) +
                Fmt::Right(Fmt::Money(line->Item->Price), 10) +
                Fmt::Right(Fmt::Money(cart->LineDiscount(line)), 8) +
                Fmt::Right(Fmt::Money(cart->LineSubtotal(line)), 10));
        }

        sb->AppendLine("----------------------------------------------");
        sb->AppendLine(Fmt::Left("Total without tax", 30) + Fmt::Right(Fmt::Money(cart->TotalWithoutTax()), 16));
        sb->AppendLine(Fmt::Left("Total tax", 30) + Fmt::Right(Fmt::Money(cart->TotalTax()), 16));
        sb->AppendLine(Fmt::Left("Total discount", 30) + Fmt::Right(Fmt::Money(cart->TotalDiscount()), 16));
        sb->AppendLine(Fmt::Left("PAYABLE (PKR)", 30) + Fmt::Right(Fmt::Money(cart->Payable()), 16));
        return sb->ToString();
    }
};

// ---------------------------------------------------------------------------
// Barcode generator window (increment 1)
// ---------------------------------------------------------------------------
public ref class BarcodeForm : public StatusForm
{
private:
    Barcode^ barcode;
    List<int>^ currentRow;

    TextBox^ txtPayload;
    PictureBox^ pic;

public:
    BarcodeForm(Barcode^ barcode)
        : StatusForm("Enter digits and click Generate (saved as <code>.svg), or load a saved barcode.")
    {
        this->barcode = barcode;
        currentRow = nullptr;
        BuildUi();
    }

private:
    void BuildUi()
    {
        this->Text = "Code 39 Barcode Tool";
        this->ClientSize = Drawing::Size(720, 420);
        this->MinimumSize = Drawing::Size(560, 320);
        this->StartPosition = FormStartPosition::CenterScreen;

        txtPayload = Ui::MakeTextBox(65, 14, 200);
        txtPayload->MaxLength = 30;

        Button^ btnPreview = Ui::MakeButton("Generate", 280, 12, 90, 27, gcnew EventHandler(this, &BarcodeForm::OnPreview));
        Button^ btnSave = Ui::MakeButton("Save...", 378, 12, 90, 27, gcnew EventHandler(this, &BarcodeForm::OnSave));
        Button^ btnLoad = Ui::MakeButton("Load...", 476, 12, 90, 27, gcnew EventHandler(this, &BarcodeForm::OnLoad));
        Button^ btnBack = Ui::MakeButton("Back", 574, 12, 90, 27, gcnew EventHandler(this, &BarcodeForm::OnBack));

        pic = gcnew PictureBox();
        pic->Location = Point(12, 52);
        pic->Size = Drawing::Size(696, 330);
        pic->Anchor = AnchorStyles::Top | AnchorStyles::Bottom | AnchorStyles::Left | AnchorStyles::Right;
        pic->BorderStyle = BorderStyle::FixedSingle;
        pic->BackColor = Color::White;
        pic->SizeMode = PictureBoxSizeMode::Zoom;

        this->Controls->Add(Ui::MakeLabel("Digits:", 12, 18));
        this->Controls->Add(txtPayload);
        this->Controls->Add(btnPreview);
        this->Controls->Add(btnSave);
        this->Controls->Add(btnLoad);
        this->Controls->Add(btnBack);
        this->Controls->Add(pic);

        this->AcceptButton = btnPreview;
        this->CancelButton = btnBack;
    }

    void ShowRow(List<int>^ row)
    {
        Image^ old = pic->Image;
        pic->Image = barcode->Render(row, 3);
        if (old != nullptr) delete old;
    }

    void OnBack(Object^ sender, EventArgs^ e)
    {
        this->Close();
    }

    // Generates the barcode and saves it right away as <code>.svg
    // (next to Product.txt, so it matches the barcode file name of the product)
    void OnPreview(Object^ sender, EventArgs^ e)
    {
        try
        {
            String^ code = txtPayload->Text->Trim();
            currentRow = barcode->Encode(code);
            ShowRow(currentRow);

            String^ path = Path::Combine(Files::Folder, code + ".svg");
            barcode->SaveSvg(currentRow, path);
            SetStatus("Generated and saved: " + path);
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot generate", ex->Message);
        }
    }

    void OnSave(Object^ sender, EventArgs^ e)
    {
        try
        {
            // Always save what the textbox says right now
            String^ code = txtPayload->Text->Trim();
            currentRow = barcode->Encode(code);
            ShowRow(currentRow);

            SaveFileDialog^ dlg = gcnew SaveFileDialog();
            dlg->Filter = "SVG barcode (*.svg)|*.svg|PNG image (*.png)|*.png";
            dlg->FileName = code;                       // the barcode's own code
            dlg->InitialDirectory = Files::Folder;
            dlg->DefaultExt = "svg";
            dlg->AddExtension = true;

            if (dlg->ShowDialog() != System::Windows::Forms::DialogResult::OK)
                return;

            if (Path::GetExtension(dlg->FileName)->ToLower() == ".png")
            {
                Bitmap^ bmp = barcode->Render(currentRow, 3);
                bmp->Save(dlg->FileName, ImageFormat::Png);
                delete bmp;
            }
            else
            {
                barcode->SaveSvg(currentRow, dlg->FileName);
            }
            SetStatus("Saved: " + dlg->FileName);
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot save", ex->Message);
        }
    }

    void OnLoad(Object^ sender, EventArgs^ e)
    {
        OpenFileDialog^ dlg = gcnew OpenFileDialog();
        dlg->Filter = "Barcode image (*.svg;*.png;*.jpg;*.jpeg;*.bmp)|*.svg;*.png;*.jpg;*.jpeg;*.bmp|SVG barcode (*.svg)|*.svg|Image (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp";

        if (dlg->ShowDialog() != System::Windows::Forms::DialogResult::OK)
            return;

        try
        {
            List<int>^ row;
            if (Path::GetExtension(dlg->FileName)->ToLower() == ".svg")
                row = barcode->LoadSvg(dlg->FileName);
            else
                row = barcode->LoadImage(dlg->FileName);
            String^ decoded = barcode->Decode(row);

            currentRow = row;
            txtPayload->Text = decoded;
            ShowRow(currentRow);
            SetStatus("Loaded " + Path::GetFileName(dlg->FileName) + " - decoded: " + decoded);
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot read barcode", ex->Message);
        }
    }
};

// ---------------------------------------------------------------------------
// Product description window (cashier or manager scans a barcode)
// ---------------------------------------------------------------------------
public ref class ProductForm : public StatusForm
{
private:
    IProductLookup^ products;
    ScannerControl^ scanner;
    TextBox^ txtInfo;

public:
    ProductForm(IProductLookup^ products, ScannerControl^ scanner)
        : StatusForm("Show a product barcode to the camera.")
    {
        this->products = products;
        this->scanner = scanner;
        BuildUi();
        SetStatus(products->Summary());
    }

private:
    void BuildUi()
    {
        this->Text = "Product Description";
        this->ClientSize = Drawing::Size(820, 420);
        this->StartPosition = FormStartPosition::CenterScreen;

        scanner->Location = Point(12, 12);
        scanner->Size = Drawing::Size(400, 330);
        scanner->BarcodeScanned += gcnew BarcodeHandler(this, &ProductForm::OnBarcodeScanned);

        txtInfo = Ui::MakeViewer(430, 12, 378, 330);

        Button^ btnBack = Ui::MakeButton("Back", 718, 350, 90, 27, gcnew EventHandler(this, &ProductForm::OnBack));

        this->Controls->Add(scanner);
        this->Controls->Add(txtInfo);
        this->Controls->Add(btnBack);

        this->CancelButton = btnBack;
    }

    void OnBack(Object^ sender, EventArgs^ e)
    {
        this->Close();
    }

    void OnBarcodeScanned(String^ payload)
    {
        try
        {
            Product^ product = products->FindByBarcode(payload);
            if (product == nullptr)
            {
                txtInfo->Text = "Scanned barcode " + payload + " is not in Product.txt." +
                    Environment::NewLine + Environment::NewLine + products->Summary();
                SetStatus("Unknown barcode: " + payload);
                return;
            }

            txtInfo->Text = ProductText::Describe(product);
            SetStatus("Scanned: " + payload);
        }
        catch (Exception^ ex)
        {
            txtInfo->Text = "Error: " + ex->Message;
        }
    }
};

// ---------------------------------------------------------------------------
// Bill window
// ---------------------------------------------------------------------------
public ref class BillForm : public Form
{
public:
    BillForm(Bill^ bill)
    {
        this->Text = "Bill " + bill->Id;
        this->ClientSize = Drawing::Size(520, 480);
        this->StartPosition = FormStartPosition::CenterParent;

        TextBox^ txtBill = Ui::MakeViewer(12, 12, 496, 420);
        txtBill->Text = BillText::Render(bill);

        Button^ btnClose = Ui::MakeButton("Close", 418, 442, 90, 27, nullptr);
        btnClose->DialogResult = System::Windows::Forms::DialogResult::OK;

        this->Controls->Add(txtBill);
        this->Controls->Add(btnClose);

        this->AcceptButton = btnClose;
    }
};

// ---------------------------------------------------------------------------
// Small dialog: ask for a quantity
// ---------------------------------------------------------------------------
public ref class QuantityForm : public Form
{
private:
    NumericUpDown^ numQuantity;

public:
    QuantityForm(String^ title, String^ prompt, double current, double maximum)
    {
        maximum = Math::Min(Math::Max(maximum, 0.01), 1000000.0);
        current = Math::Min(Math::Max(current, 0.01), maximum);

        this->Text = title;
        this->ClientSize = Drawing::Size(360, 130);
        this->StartPosition = FormStartPosition::CenterParent;
        this->FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedDialog;
        this->MaximizeBox = false;
        this->MinimizeBox = false;

        Label^ lblPrompt = Ui::MakeLabel(prompt, 12, 12);
        lblPrompt->AutoSize = false;
        lblPrompt->Size = Drawing::Size(336, 36);

        numQuantity = gcnew NumericUpDown();
        numQuantity->Location = Point(12, 56);
        numQuantity->Size = Drawing::Size(120, 23);
        numQuantity->DecimalPlaces = 2;
        numQuantity->Minimum = Decimal(0.01);
        numQuantity->Maximum = Decimal(maximum);
        numQuantity->Value = Decimal(current);

        Button^ btnOk = Ui::MakeButton("OK", 168, 92, 85, 27, nullptr);
        btnOk->DialogResult = System::Windows::Forms::DialogResult::OK;

        Button^ btnCancel = Ui::MakeButton("Cancel", 263, 92, 85, 27, nullptr);
        btnCancel->DialogResult = System::Windows::Forms::DialogResult::Cancel;

        this->Controls->Add(lblPrompt);
        this->Controls->Add(numQuantity);
        this->Controls->Add(btnOk);
        this->Controls->Add(btnCancel);

        this->AcceptButton = btnOk;
        this->CancelButton = btnCancel;
    }

    property double Quantity
    {
        double get() { return Decimal::ToDouble(numQuantity->Value); }
    }
};

// ---------------------------------------------------------------------------
// Small dialog: pick a held order to resume
// ---------------------------------------------------------------------------
public ref class HeldOrdersForm : public Form
{
private:
    List<Order^>^ orders;
    ListView^ list;

public:
    HeldOrdersForm(List<Order^>^ held)
    {
        orders = held;

        this->Text = "Held Orders";
        this->ClientSize = Drawing::Size(560, 340);
        this->StartPosition = FormStartPosition::CenterParent;
        this->FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedDialog;
        this->MaximizeBox = false;
        this->MinimizeBox = false;

        list = gcnew ListView();
        list->Location = Point(12, 12);
        list->Size = Drawing::Size(536, 280);
        list->View = View::Details;
        list->FullRowSelect = true;
        list->MultiSelect = false;
        list->Columns->Add("Order Id", 90);
        list->Columns->Add("Date", 90);
        list->Columns->Add("Time", 70);
        list->Columns->Add("Customer", 190);
        list->Columns->Add("Amount", 80);
        list->DoubleClick += gcnew EventHandler(this, &HeldOrdersForm::OnResume);

        for each(Order ^ order in orders)
        {
            ListViewItem^ row = gcnew ListViewItem(order->Id);
            row->SubItems->Add(order->Date);
            row->SubItems->Add(order->Time);
            row->SubItems->Add(order->CustomerName);
            row->SubItems->Add(Fmt::Money(order->Payment));
            list->Items->Add(row);
        }

        Button^ btnResume = Ui::MakeButton("Resume", 368, 302, 85, 27, gcnew EventHandler(this, &HeldOrdersForm::OnResume));
        Button^ btnCancel = Ui::MakeButton("Cancel", 463, 302, 85, 27, nullptr);
        btnCancel->DialogResult = System::Windows::Forms::DialogResult::Cancel;

        this->Controls->Add(list);
        this->Controls->Add(btnResume);
        this->Controls->Add(btnCancel);

        this->CancelButton = btnCancel;
    }

    // The order the cashier picked, nullptr when nothing is selected
    property Order^ SelectedOrder
    {
        Order ^ get()
        {
            if (list->SelectedIndices->Count == 0)
                return nullptr;
            return orders[list->SelectedIndices[0]];
        }
    }

private:
    void OnResume(Object^ sender, EventArgs^ e)
    {
        if (list->SelectedIndices->Count == 0)
            return;
        this->DialogResult = System::Windows::Forms::DialogResult::OK;
    }
};

// ---------------------------------------------------------------------------
// Order window (cashier records an order and generates the bill)
// ---------------------------------------------------------------------------
public ref class OrderForm : public StatusForm
{
private:
    IProductLookup^ products;
    IOrderWorkflow^ workflow;
    Cart^ cart;
    Order^ current;             // the held order being worked on, nullptr for a new order
    Product^ lastProduct;

    ScannerControl^ scanner;
    TextBox^ txtCustomer;
    TextBox^ txtContact;
    ComboBox^ cmbMode;
    TextBox^ txtMerchant;
    NumericUpDown^ numQty;
    ListView^ list;
    Label^ lblTotal;

public:
    OrderForm(IProductLookup^ products, IOrderWorkflow^ workflow, ScannerControl^ scanner)
        : StatusForm("Set the quantity, then show a product barcode to the camera.")
    {
        this->products = products;
        this->workflow = workflow;
        this->scanner = scanner;
        cart = workflow->NewCart();
        current = nullptr;
        lastProduct = nullptr;
        BuildUi();
        SetStatus(products->Summary());
    }

private:
    void BuildUi()
    {
        this->Text = "Record Order";
        this->ClientSize = Drawing::Size(910, 550);
        this->StartPosition = FormStartPosition::CenterScreen;

        BuildLeftSide();
        BuildRightSide();

        Button^ btnBack = Ui::MakeButton("Back", 590, 484, 90, 34, gcnew EventHandler(this, &OrderForm::OnBack));
        this->Controls->Add(btnBack);
        this->CancelButton = btnBack;
        this->FormClosing += gcnew FormClosingEventHandler(this, &OrderForm::OnClosing);
    }

    // Camera + customer, payment and quantity fields
    void BuildLeftSide()
    {
        scanner->Location = Point(12, 12);
        scanner->Size = Drawing::Size(400, 300);
        scanner->BarcodeScanned += gcnew BarcodeHandler(this, &OrderForm::OnBarcodeScanned);

        txtCustomer = Ui::MakeTextBox(110, 330, 300);
        txtContact = Ui::MakeTextBox(110, 362, 300);
        txtMerchant = Ui::MakeTextBox(110, 426, 300);

        cmbMode = gcnew ComboBox();
        cmbMode->Location = Point(110, 394);
        cmbMode->Size = Drawing::Size(300, 23);
        cmbMode->DropDownStyle = ComboBoxStyle::DropDownList;
        for each(String ^ mode in workflow->PaymentModeNames())
            cmbMode->Items->Add(mode);
        cmbMode->SelectedIndex = 0;

        numQty = gcnew NumericUpDown();
        numQty->Location = Point(110, 458);
        numQty->Size = Drawing::Size(100, 23);
        numQty->DecimalPlaces = 2;
        numQty->Minimum = Decimal(0.01);
        numQty->Maximum = Decimal(1000);
        numQty->Value = Decimal(1);

        this->Controls->Add(scanner);
        this->Controls->Add(Ui::MakeLabel("Customer:", 12, 334));
        this->Controls->Add(txtCustomer);
        this->Controls->Add(Ui::MakeLabel("Contact:", 12, 366));
        this->Controls->Add(txtContact);
        this->Controls->Add(Ui::MakeLabel("Payment mode:", 12, 398));
        this->Controls->Add(cmbMode);
        this->Controls->Add(Ui::MakeLabel("Merchant:", 12, 430));
        this->Controls->Add(txtMerchant);
        this->Controls->Add(Ui::MakeLabel("Quantity:", 12, 462));
        this->Controls->Add(numQty);
    }

    // Order list, total and the buttons
    void BuildRightSide()
    {
        list = gcnew ListView();
        list->Location = Point(430, 12);
        list->Size = Drawing::Size(468, 340);
        list->View = View::Details;
        list->FullRowSelect = true;
        list->Columns->Add("Item", 70);
        list->Columns->Add("Name", 170);
        list->Columns->Add("Qty", 60);
        list->Columns->Add("Price", 80);
        list->Columns->Add("Total", 80);
        list->DoubleClick += gcnew EventHandler(this, &OrderForm::OnChangeQty);
        list->KeyDown += gcnew KeyEventHandler(this, &OrderForm::OnListKey);

        lblTotal = Ui::MakeLabel("Payable: PKR 0.00", 430, 402);
        lblTotal->Font = gcnew Drawing::Font("Segoe UI", 12.0f, FontStyle::Bold);

        this->Controls->Add(list);
        this->Controls->Add(lblTotal);
        this->Controls->Add(Ui::MakeButton("Change Qty", 430, 360, 110, 28, gcnew EventHandler(this, &OrderForm::OnChangeQty)));
        this->Controls->Add(Ui::MakeButton("Remove", 548, 360, 100, 28, gcnew EventHandler(this, &OrderForm::OnRemove)));
        this->Controls->Add(Ui::MakeButton("Clear All", 656, 360, 100, 28, gcnew EventHandler(this, &OrderForm::OnClear)));
        this->Controls->Add(Ui::MakeButton("Description", 430, 440, 110, 30, gcnew EventHandler(this, &OrderForm::OnDescription)));
        this->Controls->Add(Ui::MakeButton("Hold Order", 548, 440, 110, 30, gcnew EventHandler(this, &OrderForm::OnHold)));
        this->Controls->Add(Ui::MakeButton("Held Orders...", 666, 440, 120, 30, gcnew EventHandler(this, &OrderForm::OnResume)));
        this->Controls->Add(Ui::MakeButton("Generate Bill", 430, 484, 150, 34, gcnew EventHandler(this, &OrderForm::OnBill)));
    }

    void OnBack(Object^ sender, EventArgs^ e)
    {
        this->Close();
    }

    // Leaving the screen with a resumed order that was not paid or held again:
    // it goes back to on-hold instead of staying in-progress forever
    void OnClosing(Object^ sender, FormClosingEventArgs^ e)
    {
        if (current == nullptr)
            return;

        try
        {
            workflow->Release(current);
        }
        catch (Exception^)
        {
            // nothing more can be done while closing
        }
    }

    OrderDraft^ ReadDraft()
    {
        return gcnew OrderDraft(txtCustomer->Text->Trim(), txtContact->Text->Trim(),
            cmbMode->Text, txtMerchant->Text->Trim());
    }

    // Cart -> list on the screen
    void RefreshList()
    {
        list->Items->Clear();
        for each(OrderItem ^ line in cart->Items)
        {
            ListViewItem^ row = gcnew ListViewItem(line->Item->Acronym);
            row->SubItems->Add(line->Item->Name);
            row->SubItems->Add(Fmt::Number(line->Quantity));
            row->SubItems->Add(Fmt::Money(line->Item->Price));
            row->SubItems->Add(Fmt::Money(cart->LineSubtotal(line)));
            list->Items->Add(row);
        }
        lblTotal->Text = "Payable: PKR " + Fmt::Money(cart->Payable());
    }

    // Clears the screen for the next customer
    void NewOrder()
    {
        cart = workflow->NewCart();
        current = nullptr;
        lastProduct = nullptr;
        txtCustomer->Clear();
        txtContact->Clear();
        txtMerchant->Clear();
        cmbMode->SelectedIndex = 0;
        numQty->Value = Decimal(1);
        RefreshList();
        SetStatus("Ready for the next order.");
    }

    // Shows a resumed order on the screen
    void ShowOrder(Order^ order)
    {
        txtCustomer->Text = order->CustomerName;
        txtContact->Text = order->CustomerContact;
        txtMerchant->Text = order->Merchant;
        cmbMode->SelectedItem = order->PaymentMode;
        RefreshList();
        SetStatus("Order " + order->Id + " resumed (in-progress).");
    }

    void OnBarcodeScanned(String^ payload)
    {
        try
        {
            Product^ product = products->FindByBarcode(payload);
            if (product == nullptr)
            {
                Alert("Barcode " + payload + " was read, but it is not in Product.txt." +
                    Environment::NewLine + Environment::NewLine + products->Summary());
                return;
            }

            lastProduct = product;
            double qty = Decimal::ToDouble(numQty->Value);
            if (!cart->Add(product, qty))
            {
                Alert("Not enough stock of " + product->Acronym + " (available: " +
                    Fmt::Number(product->Quantity) + ", wanted: " + Fmt::Number(qty) + ").");
                return;
            }

            RefreshList();
            SetStatus("Added " + product->Acronym + " x " + Fmt::Number(qty) +
                " at PKR " + Fmt::Money(product->Price));
        }
        catch (Exception^ ex)
        {
            Alert("Scan error: " + ex->Message);
        }
    }

    // Message box that does not let the camera read the same barcode again meanwhile
    void Alert(String^ message)
    {
        SetStatus(message);
        scanner->Pause();
        Ui::Warn("Scan", message);
        scanner->Resume();
    }

    // The cart line selected in the list, nullptr when nothing is selected
    OrderItem^ SelectedLine()
    {
        if (list->SelectedIndices->Count == 0)
            return nullptr;
        return cart->Items[list->SelectedIndices[0]];
    }

    void SelectRow(int index)
    {
        if (index >= 0 && index < list->Items->Count)
            list->Items[index]->Selected = true;
    }

    // Change the quantity of the selected line (button or double click)
    void OnChangeQty(Object^ sender, EventArgs^ e)
    {
        OrderItem^ line = SelectedLine();
        if (line == nullptr)
        {
            SetStatus("Select a product in the list first.");
            return;
        }
        int index = list->SelectedIndices[0];

        scanner->Pause();
        QuantityForm^ dlg = gcnew QuantityForm("Change quantity",
            line->Item->Acronym + " - " + line->Item->Name +
            "   (in stock: " + Fmt::Number(line->Item->Quantity) + ")",
            line->Quantity, line->Item->Quantity);
        System::Windows::Forms::DialogResult result = dlg->ShowDialog();
        double quantity = dlg->Quantity;
        delete dlg;
        scanner->Resume();

        if (result != System::Windows::Forms::DialogResult::OK)
            return;

        if (!cart->SetQuantity(line, quantity))
        {
            SetStatus("Not enough stock of " + line->Item->Acronym +
                " (available: " + Fmt::Number(line->Item->Quantity) + ")");
            return;
        }

        RefreshList();
        SelectRow(index);
        SetStatus("Quantity of " + line->Item->Acronym + " is now " + Fmt::Number(quantity));
    }

    void RemoveSelected()
    {
        OrderItem^ line = SelectedLine();
        if (line == nullptr)
        {
            SetStatus("Select a product in the list first.");
            return;
        }
        int index = list->SelectedIndices[0];

        cart->Remove(line);
        RefreshList();
        SelectRow(Math::Min(index, list->Items->Count - 1));
        SetStatus("Removed " + line->Item->Acronym);
    }

    void OnRemove(Object^ sender, EventArgs^ e)
    {
        RemoveSelected();
    }

    // Delete key removes the selected line
    void OnListKey(Object^ sender, KeyEventArgs^ e)
    {
        if (e->KeyCode == Keys::Delete)
            RemoveSelected();
    }

    void OnClear(Object^ sender, EventArgs^ e)
    {
        if (cart->Items->Count == 0)
        {
            SetStatus("The order is already empty.");
            return;
        }

        scanner->Pause();
        System::Windows::Forms::DialogResult answer = MessageBox::Show(
            "Remove all products from this order?", "Clear order",
            MessageBoxButtons::YesNo, MessageBoxIcon::Question);
        scanner->Resume();

        if (answer != System::Windows::Forms::DialogResult::Yes)
            return;

        cart->Clear();
        lastProduct = nullptr;
        RefreshList();
        SetStatus("Order cleared.");
    }

    // Describes the selected line, otherwise the last scanned product
    void OnDescription(Object^ sender, EventArgs^ e)
    {
        Product^ product = lastProduct;
        OrderItem^ line = SelectedLine();
        if (line != nullptr)
            product = line->Item;

        if (product == nullptr)
        {
            SetStatus("Scan or select a product first.");
            return;
        }

        scanner->Pause();
        Ui::Info("Product description", ProductText::Describe(product));
        scanner->Resume();
    }

    // Saves the order as on-hold and clears the screen
    void OnHold(Object^ sender, EventArgs^ e)
    {
        try
        {
            Order^ order = workflow->Hold(current, ReadDraft(), cart);
            current = nullptr;

            scanner->Pause();
            Ui::Info("Order on hold", "Order " + order->Id + " is on hold. Use \"Held Orders...\" to continue it later.");
            scanner->Resume();

            NewOrder();
            SetStatus("Order " + order->Id + " put on hold.");
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot hold order", ex->Message);
        }
    }

    // Picks a held order and loads it back onto the screen
    void OnResume(Object^ sender, EventArgs^ e)
    {
        if (current != nullptr || cart->Items->Count > 0)
        {
            SetStatus("Pay, hold or clear the current order first.");
            return;
        }

        try
        {
            List<Order^>^ held = workflow->HeldOrders();
            if (held->Count == 0)
            {
                SetStatus("There are no held orders.");
                return;
            }

            scanner->Pause();
            HeldOrdersForm^ dlg = gcnew HeldOrdersForm(held);
            System::Windows::Forms::DialogResult result = dlg->ShowDialog();
            Order^ chosen = dlg->SelectedOrder;
            delete dlg;
            scanner->Resume();

            if (result != System::Windows::Forms::DialogResult::OK || chosen == nullptr)
                return;

            cart = workflow->Resume(chosen);
            current = chosen;
            ShowOrder(chosen);
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot resume order", ex->Message);
        }
    }

    void OnBill(Object^ sender, EventArgs^ e)
    {
        try
        {
            Bill^ bill = workflow->Pay(current, ReadDraft(), cart);
            current = nullptr;          // the order is paid now

            scanner->Pause();
            BillForm^ view = gcnew BillForm(bill);
            view->ShowDialog();
            delete view;
            scanner->Resume();

            NewOrder();
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot generate bill", ex->Message);
        }
    }
};

// ---------------------------------------------------------------------------
// Main window: knows only interfaces, never a concrete class
// ---------------------------------------------------------------------------

// Builds the screens (implemented by PosFactory)
public interface class IScreenFactory
{
    Form^ CreateOrderScreen();
    Form^ CreateProductScreen();
    Form^ CreateBarcodeScreen();
};

public ref class MenuForm : public Form
{
private:
    IScreenFactory^ screens;
    IOrderArchiver^ archiver;

public:
    MenuForm(IScreenFactory^ screens, IOrderArchiver^ archiver)
    {
        this->screens = screens;
        this->archiver = archiver;
        BuildUi();
    }

private:
    void BuildUi()
    {
        this->Text = "POS";
        this->ClientSize = Drawing::Size(320, 280);
        this->StartPosition = FormStartPosition::CenterScreen;

        this->Controls->Add(Ui::MakeButton("Record Order", 40, 20, 240, 45, gcnew EventHandler(this, &MenuForm::OnOrder)));
        this->Controls->Add(Ui::MakeButton("View Product", 40, 80, 240, 45, gcnew EventHandler(this, &MenuForm::OnProduct)));
        this->Controls->Add(Ui::MakeButton("Barcode Generator", 40, 140, 240, 45, gcnew EventHandler(this, &MenuForm::OnBarcode)));
        this->Controls->Add(Ui::MakeButton("Archive Paid Orders", 40, 200, 240, 45, gcnew EventHandler(this, &MenuForm::OnArchive)));
    }

    // Shows a window and disposes it afterwards (this also switches the camera off)
    void ShowForm(Form^ form)
    {
        form->ShowDialog();
        delete form;
    }

    void OnOrder(Object^ sender, EventArgs^ e)
    {
        try
        {
            ShowForm(screens->CreateOrderScreen());
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot open order screen", ex->Message);
        }
    }

    void OnProduct(Object^ sender, EventArgs^ e)
    {
        try
        {
            ShowForm(screens->CreateProductScreen());
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot open product screen", ex->Message);
        }
    }

    void OnBarcode(Object^ sender, EventArgs^ e)
    {
        ShowForm(screens->CreateBarcodeScreen());
    }

    // Manager action: every paid order becomes archived
    void OnArchive(Object^ sender, EventArgs^ e)
    {
        try
        {
            System::Windows::Forms::DialogResult answer = MessageBox::Show(
                "Archive all paid orders?", "Archive",
                MessageBoxButtons::YesNo, MessageBoxIcon::Question);
            if (answer != System::Windows::Forms::DialogResult::Yes)
                return;

            int count = archiver->ArchivePaid();
            Ui::Info("Archive", count.ToString() + " paid order(s) archived.");
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot archive orders", ex->Message);
        }
    }
};
