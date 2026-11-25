#pragma once

// =============================================================================
// Screens.h - everything the cashier sees (and the main menu for both roles)
//   Ui                          small helpers that build labels, boxes, buttons, lists
//   StatusForm                  base form with a status line
//   ProductText / BillText      domain object -> text for the screen
//   ProductForm, BillForm, BillListForm, QuantityForm, OrderListForm, OrderForm
//   IScreenFactory, MenuForm    the main menu only knows the factory interface
//   Depends on                  interfaces of Domain.h / Services.h, Camera.h controls;
//                               no file classes and no PosFactory
// =============================================================================

#include "Common.h"
#include "Barcode.h"
#include "Domain.h"
#include "Services.h"
#include "Pdf.h"
#include "Camera.h"

// The screens the main menu can open (PosFactory builds them)
public ref class ScreenNames abstract sealed
{
public:
    literal String^ Order = "order";
    literal String^ OpenOrder = "open-order";
    literal String^ Product = "product";
    literal String^ PrintBill = "print-bill";
    literal String^ AddProduct = "add-product";
    literal String^ Inventory = "inventory";
    literal String^ Categories = "categories";
    literal String^ Seasons = "seasons";
    literal String^ Approvals = "approvals";
    literal String^ Reports = "reports";
};

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

    static NumericUpDown^ MakeNumber(int x, int y, int width, double minimum, double maximum, int decimals, double value)
    {
        NumericUpDown^ box = gcnew NumericUpDown();
        box->Location = Point(x, y);
        box->Size = Drawing::Size(width, 23);
        box->DecimalPlaces = decimals;
        box->Minimum = Decimal(minimum);
        box->Maximum = Decimal(maximum);
        box->Value = Decimal(value);
        return box;
    }

    // Drop-down list with fixed choices; the first one is selected
    static ComboBox^ MakeCombo(int x, int y, int width, array<String^>^ items)
    {
        ComboBox^ box = gcnew ComboBox();
        box->Location = Point(x, y);
        box->Size = Drawing::Size(width, 23);
        box->DropDownStyle = ComboBoxStyle::DropDownList;
        for each(String ^ item in items)
            box->Items->Add(item);
        if (box->Items->Count > 0)
            box->SelectedIndex = 0;
        return box;
    }

    static DateTimePicker^ MakeDatePicker(int x, int y, int width)
    {
        DateTimePicker^ picker = gcnew DateTimePicker();
        picker->Location = Point(x, y);
        picker->Size = Drawing::Size(width, 23);
        picker->Format = DateTimePickerFormat::Short;
        return picker;
    }

    static ListView^ MakeList(int x, int y, int width, int height, array<String^>^ headers, array<int>^ widths)
    {
        ListView^ list = gcnew ListView();
        list->Location = Point(x, y);
        list->Size = Drawing::Size(width, height);
        list->View = View::Details;
        list->FullRowSelect = true;
        list->MultiSelect = false;
        list->HideSelection = false;
        list->GridLines = true;
        for (int i = 0; i < headers->Length; i++)
            list->Columns->Add(headers[i], widths[i]);
        return list;
    }

    static void AddRow(ListView^ list, array<String^>^ cells)
    {
        ListViewItem^ row = gcnew ListViewItem(cells[0]);
        for (int i = 1; i < cells->Length; i++)
            row->SubItems->Add(cells[i]);
        list->Items->Add(row);
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

// A typed or scanned code -> product: the barcode first, then the product id
public ref class ProductFinder abstract sealed
{
public:
    static Product^ ByCode(IProductLookup^ products, String^ code)
    {
        String^ text = code->Trim();

        Product^ product = products->FindByBarcode(text);
        if (product != nullptr)
            return product;

        int id;
        if (Int32::TryParse(text, id))
            return products->FindById(id);
        return nullptr;
    }
};

// Manual entry for when the camera or the scanner does not work (it is always there,
// even when the camera works): type the barcode number or the product id and press
// Enter, or press Find... to pick the product by name. A USB barcode scanner that
// types the digits and presses Enter works through this box too.
public ref class BarcodeBox : public UserControl
{
private:
    TextBox^ txtCode;

    void Submit()
    {
        String^ code = txtCode->Text->Trim();
        txtCode->Clear();
        txtCode->Focus();
        if (code->Length > 0)
            BarcodeEntered(code);
    }

    void OnAdd(Object^ sender, EventArgs^ e)
    {
        Submit();
    }

    void OnFind(Object^ sender, EventArgs^ e)
    {
        FindRequested(this, EventArgs::Empty);
    }

    void OnKey(Object^ sender, KeyEventArgs^ e)
    {
        if (e->KeyCode == Keys::Enter)
        {
            e->Handled = true;
            e->SuppressKeyPress = true;
            Submit();
        }
    }

public:
    event BarcodeHandler^ BarcodeEntered;
    event EventHandler^ FindRequested;

    BarcodeBox()
    {
        this->Size = Drawing::Size(400, 30);

        txtCode = Ui::MakeTextBox(100, 3, 105);
        txtCode->KeyDown += gcnew KeyEventHandler(this, &BarcodeBox::OnKey);

        this->Controls->Add(Ui::MakeLabel("Barcode / no.:", 0, 7));
        this->Controls->Add(txtCode);
        this->Controls->Add(Ui::MakeButton("Add", 212, 1, 60, 27, gcnew EventHandler(this, &BarcodeBox::OnAdd)));
        this->Controls->Add(Ui::MakeButton("Find...", 280, 1, 80, 27, gcnew EventHandler(this, &BarcodeBox::OnFind)));
    }

    void FocusBox()
    {
        txtCode->Focus();
    }
};

// Pick a product by typing part of its name, acronym or barcode
public ref class ProductPickForm : public Form
{
private:
    IProductLookup^ products;
    List<Product^>^ shown;
    ListView^ list;
    TextBox^ txtFilter;

public:
    ProductPickForm(IProductLookup^ products)
    {
        this->products = products;
        shown = products->Search("");

        this->Text = "Find a product";
        this->ClientSize = Drawing::Size(680, 420);
        this->StartPosition = FormStartPosition::CenterParent;
        this->FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedDialog;
        this->MaximizeBox = false;
        this->MinimizeBox = false;

        txtFilter = Ui::MakeTextBox(130, 12, 260);
        txtFilter->TextChanged += gcnew EventHandler(this, &ProductPickForm::OnFilter);

        list = Ui::MakeList(12, 44, 656, 320,
            gcnew array<String^>{ "Barcode", "Item", "Name", "Price", "Available" },
            gcnew array<int>{ 90, 90, 270, 100, 100 });
        list->DoubleClick += gcnew EventHandler(this, &ProductPickForm::OnPick);

        Button^ btnPick = Ui::MakeButton("Select", 478, 376, 90, 30, gcnew EventHandler(this, &ProductPickForm::OnPick));
        Button^ btnCancel = Ui::MakeButton("Cancel", 578, 376, 90, 30, nullptr);
        btnCancel->DialogResult = System::Windows::Forms::DialogResult::Cancel;

        this->Controls->Add(Ui::MakeLabel("Name / item / barcode:", 12, 16));
        this->Controls->Add(txtFilter);
        this->Controls->Add(list);
        this->Controls->Add(btnPick);
        this->Controls->Add(btnCancel);

        this->AcceptButton = btnPick;
        this->CancelButton = btnCancel;
        Fill();
    }

    // The product the cashier picked, nullptr when nothing is selected
    property Product^ SelectedProduct
    {
        Product ^ get()
        {
            if (list->SelectedIndices->Count == 0)
                return nullptr;
            return shown[list->SelectedIndices[0]];
        }
    }

private:
    void Fill()
    {
        list->Items->Clear();
        for each(Product ^ product in shown)
            Ui::AddRow(list, gcnew array<String^>{ product->Barcode, product->Acronym, product->Name,
                Fmt::Money(product->Price), Fmt::Number(product->Quantity) });

        if (list->Items->Count > 0)
            list->Items[0]->Selected = true;
    }

    void OnFilter(Object^ sender, EventArgs^ e)
    {
        shown = products->Search(txtFilter->Text);
        Fill();
    }

    void OnPick(Object^ sender, EventArgs^ e)
    {
        if (list->SelectedIndices->Count == 0)
            return;
        this->DialogResult = System::Windows::Forms::DialogResult::OK;
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
        sb->AppendLine("Category     : " + product->CategoryId);
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

// A bill as printable text (screen and PDF), built from the Bill.txt / BillDetail.txt records
public ref class BillText abstract sealed
{
private:
    static String^ Row(String^ label, double value)
    {
        return Fmt::Left(label, 34) + Fmt::Right(Fmt::Money(value), 16);
    }

public:
    static String^ Render(BillRecord^ bill, List<BillDetailRecord^>^ details)
    {
        String^ rule = "--------------------------------------------------";

        StringBuilder^ sb = gcnew StringBuilder();
        sb->AppendLine("BILL " + bill->Id);
        sb->AppendLine("Order    : " + bill->OrderId + "  " + bill->Date + " " + bill->Time);
        sb->AppendLine("Customer : " + bill->CustomerName);
        sb->AppendLine("Status   : " + bill->Status);
        sb->AppendLine(rule);
        sb->AppendLine(Fmt::Left("Item", 10) + Fmt::Right("Qty", 8) + Fmt::Right("Price", 10) +
            Fmt::Right("Disc", 10) + Fmt::Right("Total", 12));

        for each(BillDetailRecord ^ line in details)
        {
            sb->AppendLine(Fmt::Left(line->Acronym, 10) +
                Fmt::Right(Fmt::Number(line->Quantity), 8) +
                Fmt::Right(Fmt::Money(line->UnitPrice), 10) +
                Fmt::Right(Fmt::Money(line->Discount), 10) +
                Fmt::Right(Fmt::Money(line->UnitPrice * line->Quantity), 12));
        }

        sb->AppendLine(rule);
        sb->AppendLine(Row("Total without tax", bill->TotalWithoutTax));
        sb->AppendLine(Row("Total tax", bill->TotalTax));
        sb->AppendLine(Row("Product discounts", bill->TotalDiscount));
        sb->AppendLine(Row("Seasonal discount", bill->SeasonalDiscount));
        sb->AppendLine(Row("Special discount", bill->SpecialDiscount));
        sb->AppendLine(Row("PAYABLE (PKR)", bill->Payable));
        sb->AppendLine(Row("Amount received", bill->AmountReceived));
        sb->AppendLine(Row("Balance returned", bill->Balance));
        return sb->ToString();
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
    BarcodeBox^ barcodeBox;
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

        barcodeBox = gcnew BarcodeBox();
        barcodeBox->Location = Point(12, 350);
        barcodeBox->BarcodeEntered += gcnew BarcodeHandler(this, &ProductForm::OnBarcodeScanned);
        barcodeBox->FindRequested += gcnew EventHandler(this, &ProductForm::OnFind);

        txtInfo = Ui::MakeViewer(430, 12, 378, 330);

        Button^ btnBack = Ui::MakeButton("Back", 718, 350, 90, 27, gcnew EventHandler(this, &ProductForm::OnBack));

        this->Controls->Add(scanner);
        this->Controls->Add(barcodeBox);
        this->Controls->Add(txtInfo);
        this->Controls->Add(btnBack);

        this->CancelButton = btnBack;
        this->Shown += gcnew EventHandler(this, &ProductForm::OnShown);
    }

    // The box has the focus at once: a typed number or a USB scanner works without a click
    void OnShown(Object^ sender, EventArgs^ e)
    {
        barcodeBox->FocusBox();
    }

    // Find the product by name when the barcode cannot be read
    void OnFind(Object^ sender, EventArgs^ e)
    {
        scanner->Pause();
        ProductPickForm^ dlg = gcnew ProductPickForm(products);
        System::Windows::Forms::DialogResult result = dlg->ShowDialog();
        Product^ chosen = dlg->SelectedProduct;
        delete dlg;
        scanner->Resume();

        if (result == System::Windows::Forms::DialogResult::OK && chosen != nullptr)
            OnBarcodeScanned(chosen->Barcode);
    }

    void OnBack(Object^ sender, EventArgs^ e)
    {
        this->Close();
    }

    void OnBarcodeScanned(String^ payload)
    {
        try
        {
            Product^ product = ProductFinder::ByCode(products, payload);
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
// Bill window: the bill on the screen and "Save as PDF"
// ---------------------------------------------------------------------------
public ref class BillForm : public Form
{
private:
    BillRecord^ record;
    String^ billText;
    IPdfExporter^ exporter;

public:
    BillForm(BillRecord^ record, List<BillDetailRecord^>^ details, IPdfExporter^ exporter)
    {
        this->record = record;
        this->exporter = exporter;
        billText = BillText::Render(record, details);

        this->Text = "Bill " + record->Id;
        this->ClientSize = Drawing::Size(560, 500);
        this->StartPosition = FormStartPosition::CenterParent;

        TextBox^ viewer = Ui::MakeViewer(12, 12, 536, 430);
        viewer->Text = billText;

        Button^ btnPdf = Ui::MakeButton("Save as PDF...", 12, 456, 150, 30, gcnew EventHandler(this, &BillForm::OnPdf));
        Button^ btnClose = Ui::MakeButton("Close", 458, 456, 90, 30, nullptr);
        btnClose->DialogResult = System::Windows::Forms::DialogResult::OK;

        this->Controls->Add(viewer);
        this->Controls->Add(btnPdf);
        this->Controls->Add(btnClose);

        this->AcceptButton = btnClose;      // Enter closes the bill: one key at the till
        this->CancelButton = btnClose;
    }

private:
    void OnPdf(Object^ sender, EventArgs^ e)
    {
        try
        {
            SaveFileDialog^ dlg = gcnew SaveFileDialog();
            dlg->Filter = "PDF file (*.pdf)|*.pdf";
            dlg->FileName = "Bill_" + record->Id;
            dlg->InitialDirectory = Files::Folder;
            dlg->DefaultExt = "pdf";
            dlg->AddExtension = true;

            if (dlg->ShowDialog() != System::Windows::Forms::DialogResult::OK)
                return;

            exporter->Export(dlg->FileName, billText);
            Ui::Info("Bill saved", "Bill " + record->Id + " was saved as " + dlg->FileName);
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot save the PDF", ex->Message);
        }
    }
};

// ---------------------------------------------------------------------------
// Print Bill: find an old bill and open it (cashier or manager)
// ---------------------------------------------------------------------------
public ref class BillListForm : public Form
{
private:
    IBillReader^ bills;
    IPdfExporter^ exporter;
    List<BillRecord^>^ all;
    List<BillRecord^>^ shown;
    ListView^ list;
    TextBox^ txtFilter;

public:
    BillListForm(IBillReader^ bills, IPdfExporter^ exporter)
    {
        this->bills = bills;
        this->exporter = exporter;
        all = bills->FindAll();
        all->Reverse();                 // newest first
        shown = all;

        this->Text = "Print Bill (PDF)";
        this->ClientSize = Drawing::Size(700, 400);
        this->StartPosition = FormStartPosition::CenterScreen;

        txtFilter = Ui::MakeTextBox(150, 12, 220);
        txtFilter->TextChanged += gcnew EventHandler(this, &BillListForm::OnFilter);

        list = Ui::MakeList(12, 44, 676, 300,
            gcnew array<String^>{ "Bill Id", "Order Id", "Date", "Customer", "Payable", "Status" },
            gcnew array<int>{ 90, 90, 90, 200, 110, 80 });
        list->DoubleClick += gcnew EventHandler(this, &BillListForm::OnOpen);

        Button^ btnOpen = Ui::MakeButton("View / Save PDF", 440, 356, 140, 30, gcnew EventHandler(this, &BillListForm::OnOpen));
        Button^ btnClose = Ui::MakeButton("Close", 598, 356, 90, 30, nullptr);
        btnClose->DialogResult = System::Windows::Forms::DialogResult::Cancel;

        this->Controls->Add(Ui::MakeLabel("Bill / order / name:", 12, 16));
        this->Controls->Add(txtFilter);
        this->Controls->Add(list);
        this->Controls->Add(btnOpen);
        this->Controls->Add(btnClose);

        this->AcceptButton = btnOpen;
        this->CancelButton = btnClose;
        Fill();
    }

private:
    void Fill()
    {
        list->Items->Clear();
        for each(BillRecord ^ bill in shown)
            Ui::AddRow(list, gcnew array<String^>{ bill->Id, bill->OrderId, bill->Date,
                bill->CustomerName, Fmt::Money(bill->Payable), bill->Status });

        if (list->Items->Count > 0)
            list->Items[0]->Selected = true;
    }

    void OnFilter(Object^ sender, EventArgs^ e)
    {
        String^ text = txtFilter->Text->Trim()->ToLower();
        shown = gcnew List<BillRecord^>();
        for each(BillRecord ^ bill in all)
        {
            if (text->Length == 0 || bill->Id->Contains(text) || bill->OrderId->Contains(text) ||
                bill->CustomerName->ToLower()->Contains(text))
                shown->Add(bill);
        }
        Fill();
    }

    void OnOpen(Object^ sender, EventArgs^ e)
    {
        if (list->SelectedIndices->Count == 0)
            return;

        try
        {
            BillRecord^ record = shown[list->SelectedIndices[0]];
            BillForm^ form = gcnew BillForm(record, bills->FindDetails(record->Id), exporter);
            form->ShowDialog();
            delete form;
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot open the bill", ex->Message);
        }
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
    QuantityForm(String^ title, String^ prompt, double current, double minimum, double maximum)
    {
        maximum = Math::Max(maximum, minimum);
        current = Math::Min(Math::Max(current, minimum), maximum);

        this->Text = title;
        this->ClientSize = Drawing::Size(360, 130);
        this->StartPosition = FormStartPosition::CenterParent;
        this->FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedDialog;
        this->MaximizeBox = false;
        this->MinimizeBox = false;

        Label^ lblPrompt = Ui::MakeLabel(prompt, 12, 12);
        lblPrompt->AutoSize = false;
        lblPrompt->Size = Drawing::Size(336, 36);

        numQuantity = Ui::MakeNumber(12, 56, 120, minimum, maximum, 2, current);

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
// Pick an order (held orders, or any order that can be opened).
// The first order is already selected: Enter or a double click opens it.
// ---------------------------------------------------------------------------
public ref class OrderListForm : public Form
{
private:
    List<Order^>^ all;
    List<Order^>^ shown;
    ListView^ list;
    TextBox^ txtFilter;

public:
    OrderListForm(List<Order^>^ orders, String^ title)
    {
        all = orders;
        shown = orders;

        this->Text = title;
        this->ClientSize = Drawing::Size(660, 400);
        this->StartPosition = FormStartPosition::CenterParent;
        this->FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedDialog;
        this->MaximizeBox = false;
        this->MinimizeBox = false;

        txtFilter = Ui::MakeTextBox(130, 12, 220);
        txtFilter->TextChanged += gcnew EventHandler(this, &OrderListForm::OnFilter);

        list = Ui::MakeList(12, 44, 636, 300,
            gcnew array<String^>{ "Order Id", "Date", "Customer", "Status", "Amount" },
            gcnew array<int>{ 90, 90, 230, 100, 110 });
        list->DoubleClick += gcnew EventHandler(this, &OrderListForm::OnOpen);

        Button^ btnOpen = Ui::MakeButton("Open", 458, 356, 90, 30, gcnew EventHandler(this, &OrderListForm::OnOpen));
        Button^ btnCancel = Ui::MakeButton("Cancel", 558, 356, 90, 30, nullptr);
        btnCancel->DialogResult = System::Windows::Forms::DialogResult::Cancel;

        this->Controls->Add(Ui::MakeLabel("Order id / name:", 12, 16));
        this->Controls->Add(txtFilter);
        this->Controls->Add(list);
        this->Controls->Add(btnOpen);
        this->Controls->Add(btnCancel);

        this->AcceptButton = btnOpen;
        this->CancelButton = btnCancel;
        Fill();
    }

    // The order the cashier picked, nullptr when nothing is selected
    property Order^ SelectedOrder
    {
        Order ^ get()
        {
            if (list->SelectedIndices->Count == 0)
                return nullptr;
            return shown[list->SelectedIndices[0]];
        }
    }

private:
    void Fill()
    {
        list->Items->Clear();
        for each(Order ^ order in shown)
            Ui::AddRow(list, gcnew array<String^>{ order->Id, order->Date, order->CustomerName,
                order->Status, Fmt::Money(order->Payment) });

        if (list->Items->Count > 0)
            list->Items[0]->Selected = true;
    }

    void OnFilter(Object^ sender, EventArgs^ e)
    {
        String^ text = txtFilter->Text->Trim()->ToLower();
        shown = gcnew List<Order^>();
        for each(Order ^ order in all)
        {
            if (text->Length == 0 || order->Id->Contains(text) || order->CustomerName->ToLower()->Contains(text))
                shown->Add(order);
        }
        Fill();
    }

    void OnOpen(Object^ sender, EventArgs^ e)
    {
        if (list->SelectedIndices->Count == 0)
            return;
        this->DialogResult = System::Windows::Forms::DialogResult::OK;
    }
};

// ---------------------------------------------------------------------------
// Order window (cashier records, holds, opens and pays orders).
// Few clicks: a scan adds the product at once, F2 holds, F3 resumes, F4 pays.
// ---------------------------------------------------------------------------
public ref class OrderForm : public StatusForm
{
private:
    IProductLookup^ products;
    IOrderWorkflow^ workflow;
    IPdfExporter^ exporter;
    bool openFirst;
    Cart^ cart;
    Order^ current;             // the existing order being worked on, nullptr for a new order
    Product^ lastProduct;

    ScannerControl^ scanner;
    BarcodeBox^ barcodeBox;
    TextBox^ txtCustomer;
    TextBox^ txtContact;
    ComboBox^ cmbMode;
    TextBox^ txtMerchant;
    NumericUpDown^ numQty;
    NumericUpDown^ numSpecial;
    TextBox^ txtReceived;
    ListView^ list;
    Label^ lblTotal;
    Label^ lblBreakdown;
    Button^ btnHeld;

public:
    OrderForm(IProductLookup^ products, IOrderWorkflow^ workflow, ScannerControl^ scanner,
        IPdfExporter^ exporter, bool openFirst)
        : StatusForm("Scan or type the barcode.  F2 hold   F3 held orders   F4 bill   F5 find product")
    {
        this->products = products;
        this->workflow = workflow;
        this->scanner = scanner;
        this->exporter = exporter;
        this->openFirst = openFirst;
        cart = workflow->NewCart();
        current = nullptr;
        lastProduct = nullptr;
        BuildUi();
        UpdateHeldButton();
        RefreshTotals();
    }

private:
    void BuildUi()
    {
        this->Text = "Record Order";
        this->ClientSize = Drawing::Size(910, 540);
        this->StartPosition = FormStartPosition::CenterScreen;
        this->KeyPreview = true;

        BuildLeftSide();
        BuildRightSide();

        Button^ btnBack = Ui::MakeButton("Back", 746, 464, 90, 40, gcnew EventHandler(this, &OrderForm::OnBack));
        this->Controls->Add(btnBack);
        this->CancelButton = btnBack;

        this->FormClosing += gcnew FormClosingEventHandler(this, &OrderForm::OnClosing);
        this->KeyDown += gcnew KeyEventHandler(this, &OrderForm::OnKey);
        this->Shown += gcnew EventHandler(this, &OrderForm::OnShown);
    }

    // Camera + barcode box + customer, payment and quantity fields
    void BuildLeftSide()
    {
        scanner->Location = Point(12, 12);
        scanner->Size = Drawing::Size(400, 262);
        scanner->BarcodeScanned += gcnew BarcodeHandler(this, &OrderForm::OnBarcodeScanned);

        barcodeBox = gcnew BarcodeBox();
        barcodeBox->Location = Point(12, 278);
        barcodeBox->BarcodeEntered += gcnew BarcodeHandler(this, &OrderForm::OnBarcodeScanned);
        barcodeBox->FindRequested += gcnew EventHandler(this, &OrderForm::OnFind);

        txtCustomer = Ui::MakeTextBox(110, 316, 300);
        txtContact = Ui::MakeTextBox(110, 348, 300);
        cmbMode = Ui::MakeCombo(110, 380, 300, workflow->PaymentModeNames());
        txtMerchant = Ui::MakeTextBox(110, 412, 300);
        numQty = Ui::MakeNumber(110, 444, 100, 0.01, 1000, 2, 1);
        numSpecial = Ui::MakeNumber(110, 476, 100, 0, DiscountRules::MaxSpecialPercent, 1, 0);
        numSpecial->ValueChanged += gcnew EventHandler(this, &OrderForm::OnSpecialChanged);
        txtReceived = Ui::MakeTextBox(290, 476, 120);

        this->Controls->Add(scanner);
        this->Controls->Add(barcodeBox);
        this->Controls->Add(Ui::MakeLabel("Customer:", 12, 320));
        this->Controls->Add(txtCustomer);
        this->Controls->Add(Ui::MakeLabel("Contact:", 12, 352));
        this->Controls->Add(txtContact);
        this->Controls->Add(Ui::MakeLabel("Payment mode:", 12, 384));
        this->Controls->Add(cmbMode);
        this->Controls->Add(Ui::MakeLabel("Merchant:", 12, 416));
        this->Controls->Add(txtMerchant);
        this->Controls->Add(Ui::MakeLabel("Quantity:", 12, 448));
        this->Controls->Add(numQty);
        this->Controls->Add(Ui::MakeLabel("Special %:", 12, 480));
        this->Controls->Add(numSpecial);
        this->Controls->Add(Ui::MakeLabel("Received:", 222, 480));
        this->Controls->Add(txtReceived);
    }

    // Order list, totals and the buttons
    void BuildRightSide()
    {
        list = Ui::MakeList(430, 12, 468, 296,
            gcnew array<String^>{ "Item", "Name", "Qty", "Price", "Total" },
            gcnew array<int>{ 70, 170, 60, 80, 80 });
        list->DoubleClick += gcnew EventHandler(this, &OrderForm::OnChangeQty);
        list->KeyDown += gcnew KeyEventHandler(this, &OrderForm::OnListKey);

        lblTotal = Ui::MakeLabel("Payable: PKR 0.00", 430, 352);
        lblTotal->Font = gcnew Drawing::Font("Segoe UI", 13.0f, FontStyle::Bold);
        lblBreakdown = Ui::MakeLabel("", 430, 384);

        btnHeld = Ui::MakeButton("Held Orders F3", 676, 420, 150, 32, gcnew EventHandler(this, &OrderForm::OnHeld));

        this->Controls->Add(list);
        this->Controls->Add(lblTotal);
        this->Controls->Add(lblBreakdown);
        this->Controls->Add(Ui::MakeButton("Change Qty", 430, 316, 110, 28, gcnew EventHandler(this, &OrderForm::OnChangeQty)));
        this->Controls->Add(Ui::MakeButton("Remove", 548, 316, 100, 28, gcnew EventHandler(this, &OrderForm::OnRemove)));
        this->Controls->Add(Ui::MakeButton("Clear All", 656, 316, 100, 28, gcnew EventHandler(this, &OrderForm::OnClear)));
        this->Controls->Add(Ui::MakeButton("Description", 430, 420, 110, 32, gcnew EventHandler(this, &OrderForm::OnDescription)));
        this->Controls->Add(Ui::MakeButton("Hold Order F2", 548, 420, 120, 32, gcnew EventHandler(this, &OrderForm::OnHold)));
        this->Controls->Add(btnHeld);
        this->Controls->Add(Ui::MakeButton("Generate Bill F4", 430, 464, 170, 40, gcnew EventHandler(this, &OrderForm::OnBill)));
        this->Controls->Add(Ui::MakeButton("Open Order...", 608, 464, 130, 40, gcnew EventHandler(this, &OrderForm::OnOpen)));
    }

    void OnBack(Object^ sender, EventArgs^ e)
    {
        this->Close();
    }

    // The "Open / Edit Order" menu entry shows the order list as soon as the screen is up
    void OnShown(Object^ sender, EventArgs^ e)
    {
        if (openFirst)
            OnOpen(nullptr, nullptr);

        barcodeBox->FocusBox();         // a typed number or a USB scanner works without a click
    }

    // Function keys for the till: F2 hold, F3 held orders, F4 generate bill
    void OnKey(Object^ sender, KeyEventArgs^ e)
    {
        if (e->KeyCode == Keys::F2)
        {
            e->Handled = true;
            OnHold(nullptr, nullptr);
        }
        else if (e->KeyCode == Keys::F3)
        {
            e->Handled = true;
            OnHeld(nullptr, nullptr);
        }
        else if (e->KeyCode == Keys::F4)
        {
            e->Handled = true;
            OnBill(nullptr, nullptr);
        }
        else if (e->KeyCode == Keys::F5)
        {
            e->Handled = true;
            OnFind(nullptr, nullptr);
        }
    }

    // Leaving the screen with a resumed order that was not paid or held again:
    // it goes back to on-hold instead of staying in-progress forever.
    // (A paid order that was being edited simply stays as it is.)
    void OnClosing(Object^ sender, FormClosingEventArgs^ e)
    {
        if (current == nullptr || current->Status != OrderStatus::InProgress)
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

    // Warning box that does not let the camera read the same barcode again meanwhile
    void Warn(String^ title, String^ message)
    {
        scanner->Pause();
        Ui::Warn(title, message);
        scanner->Resume();
    }

    void Alert(String^ message)
    {
        SetStatus(message);
        Warn("Scan", message);
    }

    // Typed values -> OrderDraft. The amount received only matters when paying.
    OrderDraft^ ReadDraft(bool forPayment)
    {
        double received = -1;
        String^ typed = txtReceived->Text->Trim();
        if (forPayment && typed->Length > 0)
        {
            if (!Fmt::IsNumber(typed))
                throw gcnew ArgumentException("The amount received must be a number.");
            received = Fmt::Parse(typed);
        }

        return gcnew OrderDraft(txtCustomer->Text->Trim(), txtContact->Text->Trim(), cmbMode->Text,
            txtMerchant->Text->Trim(), Decimal::ToDouble(numSpecial->Value), received);
    }

    void UpdateHeldButton()
    {
        try
        {
            btnHeld->Text = "Held Orders (" + Convert::ToString(workflow->HeldOrders()->Count) + ") F3";
        }
        catch (Exception^)
        {
            btnHeld->Text = "Held Orders F3";
        }
    }

    // Payable amount, with the seasonal and special discount
    void RefreshTotals()
    {
        try
        {
            PriceQuote^ quote = workflow->Quote(cart, Decimal::ToDouble(numSpecial->Value));
            lblTotal->Text = "Payable: PKR " + Fmt::Money(quote->Payable);
            lblBreakdown->Text = "Items " + Fmt::Money(quote->Base) + "    Seasonal discount -" +
                Fmt::Money(quote->Seasonal) + "    Special discount -" + Fmt::Money(quote->Special);
        }
        catch (Exception^ ex)
        {
            lblBreakdown->Text = ex->Message;
        }
    }

    void OnSpecialChanged(Object^ sender, EventArgs^ e)
    {
        RefreshTotals();
    }

    // Cart -> list on the screen
    void RefreshList()
    {
        list->Items->Clear();
        for each(OrderItem ^ line in cart->Items)
            Ui::AddRow(list, gcnew array<String^>{ line->Item->Acronym, line->Item->Name,
                Fmt::Number(line->Quantity), Fmt::Money(line->Item->Price), Fmt::Money(cart->LineSubtotal(line)) });
        RefreshTotals();
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
        txtReceived->Clear();
        cmbMode->SelectedIndex = 0;
        numQty->Value = Decimal(1);
        numSpecial->Value = Decimal(0);
        RefreshList();
    }

    // Shows an opened order on the screen
    void ShowOrder(Order^ order)
    {
        txtCustomer->Text = order->CustomerName;
        txtContact->Text = order->CustomerContact;
        txtMerchant->Text = order->Merchant;
        cmbMode->SelectedItem = order->PaymentMode;
        txtReceived->Clear();
        numSpecial->Value = Decimal(0);
        RefreshList();

        if (order->Status == OrderStatus::Paid)
            SetStatus("Editing paid order " + order->Id + ": change it, then press F4 for the new bill.");
        else
            SetStatus("Order " + order->Id + " opened (in-progress).");
    }

    void OnBarcodeScanned(String^ payload)
    {
        try
        {
            Product^ product = ProductFinder::ByCode(products, payload);
            if (product == nullptr)
            {
                Alert("Barcode or product number " + payload + " was not found in Product.txt." +
                    Environment::NewLine + Environment::NewLine + products->Summary());
                return;
            }

            AddProduct(product);
        }
        catch (Exception^ ex)
        {
            Alert("Scan error: " + ex->Message);
        }
    }

    // Adds a scanned, typed or picked product with the quantity of the quantity box
    void AddProduct(Product^ product)
    {
        lastProduct = product;
        double qty = Decimal::ToDouble(numQty->Value);
        if (!cart->Add(product, qty))
        {
            Alert("Not enough stock of " + product->Acronym + " (available: " +
                Fmt::Number(product->Quantity) + ", wanted: " + Fmt::Number(qty) + ").");
            return;
        }

        numQty->Value = Decimal(1);         // the next scan adds one piece again
        RefreshList();
        SetStatus("Added " + product->Acronym + " x " + Fmt::Number(qty) +
            " at PKR " + Fmt::Money(product->Price));
    }

    // Find a product by name when the barcode cannot be read or typed
    void OnFind(Object^ sender, EventArgs^ e)
    {
        scanner->Pause();
        ProductPickForm^ dlg = gcnew ProductPickForm(products);
        System::Windows::Forms::DialogResult result = dlg->ShowDialog();
        Product^ chosen = dlg->SelectedProduct;
        delete dlg;
        scanner->Resume();

        if (result != System::Windows::Forms::DialogResult::OK || chosen == nullptr)
            return;

        try
        {
            AddProduct(chosen);
        }
        catch (Exception^ ex)
        {
            Alert("Cannot add the product: " + ex->Message);
        }
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
            line->Quantity, 0.01, 1000000);
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

    // Hold: ONE press, no question, the screen is ready for the next customer
    void OnHold(Object^ sender, EventArgs^ e)
    {
        try
        {
            Order^ order = workflow->Hold(current, ReadDraft(false), cart);
            NewOrder();
            UpdateHeldButton();
            SetStatus("Order " + order->Id + " is on hold.");
        }
        catch (Exception^ ex)
        {
            SetStatus(ex->Message);
        }
    }

    // Opens the order that is chosen in the list
    void OpenOrder(Order^ order)
    {
        try
        {
            cart = workflow->Open(order);
            current = order;
            ShowOrder(order);
        }
        catch (Exception^ ex)
        {
            Warn("Cannot open the order", ex->Message);
        }
    }

    Order^ ChooseOrder(List<Order^>^ orders, String^ title)
    {
        scanner->Pause();
        OrderListForm^ dlg = gcnew OrderListForm(orders, title);
        System::Windows::Forms::DialogResult result = dlg->ShowDialog();
        Order^ chosen = dlg->SelectedOrder;
        delete dlg;
        scanner->Resume();

        if (result != System::Windows::Forms::DialogResult::OK)
            return nullptr;
        return chosen;
    }

    bool IsBusy()
    {
        if (current == nullptr && cart->Items->Count == 0)
            return false;

        SetStatus("Pay, hold or clear the current order first.");
        return true;
    }

    // Held orders: ONE held order opens at once, several are shown in a list
    void OnHeld(Object^ sender, EventArgs^ e)
    {
        if (IsBusy())
            return;

        try
        {
            List<Order^>^ held = workflow->HeldOrders();
            if (held->Count == 0)
            {
                SetStatus("There are no held orders.");
                return;
            }

            Order^ chosen = held[0];
            if (held->Count > 1)
                chosen = ChooseOrder(held, "Held orders");

            if (chosen != nullptr)
                OpenOrder(chosen);
        }
        catch (Exception^ ex)
        {
            Warn("Cannot read the held orders", ex->Message);
        }
    }

    // Any order that is not archived (paid orders need the manager's approval)
    void OnOpen(Object^ sender, EventArgs^ e)
    {
        if (IsBusy())
            return;

        try
        {
            List<Order^>^ orders = workflow->EditableOrders();
            if (orders->Count == 0)
            {
                SetStatus("There are no orders to open.");
                return;
            }

            Order^ chosen = ChooseOrder(orders, "Open an order");
            if (chosen != nullptr)
                OpenOrder(chosen);
        }
        catch (Exception^ ex)
        {
            Warn("Cannot read the orders", ex->Message);
        }
    }

    void OnBill(Object^ sender, EventArgs^ e)
    {
        try
        {
            Bill^ bill = workflow->Pay(current, ReadDraft(true), cart);
            current = nullptr;          // the order is paid now

            scanner->Pause();
            BillForm^ view = gcnew BillForm(bill->ToRecord(), bill->ToDetailRecords(), exporter);
            view->ShowDialog();
            delete view;
            scanner->Resume();

            NewOrder();
            UpdateHeldButton();
            SetStatus("Bill " + bill->Id + " saved. Ready for the next order.");
        }
        catch (Exception^ ex)
        {
            Warn("Cannot generate the bill", ex->Message);
        }
    }
};

// ---------------------------------------------------------------------------
// Main window: a cashier menu and a manager menu. It knows only interfaces,
// never a concrete class.
// ---------------------------------------------------------------------------

// Manager login: the manager menu opens only after a correct user name and password
public ref class LoginForm : public Form
{
private:
    IAuthenticator^ authenticator;
    TextBox^ txtUser;
    TextBox^ txtPassword;
    Label^ lblMessage;

public:
    LoginForm(IAuthenticator^ authenticator)
    {
        this->authenticator = authenticator;

        this->Text = "Manager login";
        this->ClientSize = Drawing::Size(340, 190);
        this->StartPosition = FormStartPosition::CenterParent;
        this->FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedDialog;
        this->MaximizeBox = false;
        this->MinimizeBox = false;

        txtUser = Ui::MakeTextBox(110, 20, 200);
        txtPassword = Ui::MakeTextBox(110, 58, 200);
        txtPassword->UseSystemPasswordChar = true;

        lblMessage = Ui::MakeLabel("", 20, 96);
        lblMessage->ForeColor = Color::Firebrick;

        Button^ btnLogin = Ui::MakeButton("Login", 130, 130, 85, 30, gcnew EventHandler(this, &LoginForm::OnLogin));
        Button^ btnCancel = Ui::MakeButton("Cancel", 225, 130, 85, 30, nullptr);
        btnCancel->DialogResult = System::Windows::Forms::DialogResult::Cancel;

        this->Controls->Add(Ui::MakeLabel("User name:", 20, 24));
        this->Controls->Add(txtUser);
        this->Controls->Add(Ui::MakeLabel("Password:", 20, 62));
        this->Controls->Add(txtPassword);
        this->Controls->Add(lblMessage);
        this->Controls->Add(btnLogin);
        this->Controls->Add(btnCancel);

        this->AcceptButton = btnLogin;
        this->CancelButton = btnCancel;
    }

private:
    void OnLogin(Object^ sender, EventArgs^ e)
    {
        if (authenticator->Login(txtUser->Text, txtPassword->Text))
        {
            this->DialogResult = System::Windows::Forms::DialogResult::OK;
            return;
        }

        lblMessage->Text = "Wrong user name or password.";
        txtPassword->Clear();
        txtPassword->Focus();
    }
};

// Builds the screens (implemented by PosFactory)
public interface class IScreenFactory
{
    Form^ Create(String^ screen);       // one of the ScreenNames
};

public ref class MenuForm : public Form
{
private:
    IScreenFactory^ screens;
    IOrderArchiver^ archiver;
    IAuthenticator^ authenticator;
    bool managerLoggedIn;
    bool switching;                     // true while the menu changes the role itself
    RadioButton^ rdoCashier;
    RadioButton^ rdoManager;
    Panel^ cashierPanel;
    Panel^ managerPanel;

public:
    MenuForm(IScreenFactory^ screens, IOrderArchiver^ archiver, IAuthenticator^ authenticator)
    {
        this->screens = screens;
        this->archiver = archiver;
        this->authenticator = authenticator;
        managerLoggedIn = false;
        switching = false;
        BuildUi();
    }

private:
    Panel^ MakePanel()
    {
        Panel^ panel = gcnew Panel();
        panel->Location = Point(0, 44);
        panel->Size = Drawing::Size(320, 420);
        return panel;
    }

    // A menu button that opens the screen named in its Tag
    void AddScreenButton(Panel^ panel, int row, String^ text, String^ screen)
    {
        Button^ button = Ui::MakeButton(text, 40, 6 + row * 44, 240, 38, gcnew EventHandler(this, &MenuForm::OnScreen));
        button->Tag = screen;
        panel->Controls->Add(button);
    }

    void BuildUi()
    {
        this->Text = "POS";
        this->ClientSize = Drawing::Size(320, 470);
        this->StartPosition = FormStartPosition::CenterScreen;

        rdoCashier = gcnew RadioButton();
        rdoCashier->Text = "Cashier";
        rdoCashier->Location = Point(40, 14);
        rdoCashier->AutoSize = true;
        rdoCashier->Checked = true;
        rdoCashier->CheckedChanged += gcnew EventHandler(this, &MenuForm::OnRole);

        rdoManager = gcnew RadioButton();
        rdoManager->Text = "Manager";
        rdoManager->Location = Point(170, 14);
        rdoManager->AutoSize = true;
        rdoManager->CheckedChanged += gcnew EventHandler(this, &MenuForm::OnRole);

        cashierPanel = MakePanel();
        AddScreenButton(cashierPanel, 0, "Record Order", ScreenNames::Order);
        AddScreenButton(cashierPanel, 1, "Open / Edit Order", ScreenNames::OpenOrder);
        AddScreenButton(cashierPanel, 2, "View Product", ScreenNames::Product);
        AddScreenButton(cashierPanel, 3, "Print Bill (PDF)", ScreenNames::PrintBill);

        managerPanel = MakePanel();
        AddScreenButton(managerPanel, 0, "Add Product", ScreenNames::AddProduct);
        AddScreenButton(managerPanel, 1, "Products && Inventory", ScreenNames::Inventory);
        AddScreenButton(managerPanel, 2, "Categories", ScreenNames::Categories);
        AddScreenButton(managerPanel, 3, "Seasons && Discounts", ScreenNames::Seasons);
        AddScreenButton(managerPanel, 4, "Edit Approvals", ScreenNames::Approvals);
        AddScreenButton(managerPanel, 5, "Reports", ScreenNames::Reports);
        managerPanel->Controls->Add(Ui::MakeButton("Archive Paid Orders", 40, 6 + 6 * 44, 240, 38, gcnew EventHandler(this, &MenuForm::OnArchive)));
        AddScreenButton(managerPanel, 7, "View Product", ScreenNames::Product);
        AddScreenButton(managerPanel, 8, "Print Bill (PDF)", ScreenNames::PrintBill);
        managerPanel->Visible = false;

        this->Controls->Add(rdoCashier);
        this->Controls->Add(rdoManager);
        this->Controls->Add(cashierPanel);
        this->Controls->Add(managerPanel);
    }

    bool AskLogin()
    {
        LoginForm^ dlg = gcnew LoginForm(authenticator);
        System::Windows::Forms::DialogResult result = dlg->ShowDialog();
        delete dlg;
        return result == System::Windows::Forms::DialogResult::OK;
    }

    // Each role sees only its own features. The manager role needs a login; a wrong
    // login or Cancel puts the menu back to the cashier. Going back to the cashier logs out.
    void OnRole(Object^ sender, EventArgs^ e)
    {
        if (switching)
            return;

        if (rdoManager->Checked && !managerLoggedIn)
        {
            if (AskLogin())
            {
                managerLoggedIn = true;
            }
            else
            {
                switching = true;
                rdoCashier->Checked = true;
                switching = false;
            }
        }

        if (rdoCashier->Checked)
            managerLoggedIn = false;

        cashierPanel->Visible = rdoCashier->Checked;
        managerPanel->Visible = rdoManager->Checked;
    }

    // Shows a window and disposes it afterwards (this also switches the camera off)
    void ShowForm(Form^ form)
    {
        form->ShowDialog();
        delete form;
    }

    void OnScreen(Object^ sender, EventArgs^ e)
    {
        Button^ button = safe_cast<Button^>(sender);
        try
        {
            ShowForm(screens->Create(button->Tag->ToString()));
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot open " + button->Text, ex->Message);
        }
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
