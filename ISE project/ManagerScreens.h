#pragma once

// =============================================================================
// ManagerScreens.h - the screens only the manager uses
//   CategoryForm      product categories (before barcodes can be generated)
//   ProductEditForm   Add Product (makes the barcode) and edit a product
//   InventoryForm     products with their available quantity, update inventory
//   SeasonForm        seasonal discounts (Seasons.txt)
//   ApprovalForm      approve or reject the edit of a paid order
//   ReportText / ReportForm   the six reports in table form, export as PDF
//   Depends on        interfaces of ManagerServices.h / Services.h, Screens.h helpers
// =============================================================================

#include "Common.h"
#include "Barcode.h"
#include "Domain.h"
#include "Services.h"
#include "ManagerServices.h"
#include "Pdf.h"
#include "Screens.h"

// ---------------------------------------------------------------------------
// Product categories
// ---------------------------------------------------------------------------
public ref class CategoryForm : public StatusForm
{
private:
    ICategoryService^ service;
    List<ProductCategory^>^ categories;
    ListView^ list;
    TextBox^ txtId;
    TextBox^ txtName;
    TextBox^ txtAcronym;

public:
    CategoryForm(ICategoryService^ service)
        : StatusForm("Add the categories first: the category id becomes the first two digits of the barcodes.")
    {
        this->service = service;
        BuildUi();
        Reload();
    }

private:
    void BuildUi()
    {
        this->Text = "Product Categories";
        this->ClientSize = Drawing::Size(640, 420);
        this->StartPosition = FormStartPosition::CenterScreen;

        list = Ui::MakeList(12, 12, 616, 270,
            gcnew array<String^>{ "Id", "Name", "Acronym" },
            gcnew array<int>{ 80, 360, 150 });
        list->SelectedIndexChanged += gcnew EventHandler(this, &CategoryForm::OnSelect);

        txtId = Ui::MakeTextBox(110, 296, 60);
        txtId->MaxLength = 2;
        txtName = Ui::MakeTextBox(110, 328, 300);
        txtAcronym = Ui::MakeTextBox(110, 360, 150);

        Button^ btnBack = Ui::MakeButton("Back", 450, 360, 90, 28, gcnew EventHandler(this, &CategoryForm::OnBack));

        this->Controls->Add(list);
        this->Controls->Add(Ui::MakeLabel("Id (10-99):", 12, 300));
        this->Controls->Add(txtId);
        this->Controls->Add(Ui::MakeLabel("Name:", 12, 332));
        this->Controls->Add(txtName);
        this->Controls->Add(Ui::MakeLabel("Acronym:", 12, 364));
        this->Controls->Add(txtAcronym);
        this->Controls->Add(Ui::MakeButton("Add", 450, 296, 90, 28, gcnew EventHandler(this, &CategoryForm::OnAdd)));
        this->Controls->Add(Ui::MakeButton("Update", 450, 328, 90, 28, gcnew EventHandler(this, &CategoryForm::OnUpdate)));
        this->Controls->Add(btnBack);

        this->CancelButton = btnBack;
    }

    void Reload()
    {
        categories = service->All();
        list->Items->Clear();
        for each(ProductCategory ^ category in categories)
            Ui::AddRow(list, gcnew array<String^>{ category->Id, category->Name, category->Acronym });
    }

    void OnSelect(Object^ sender, EventArgs^ e)
    {
        if (list->SelectedIndices->Count == 0)
            return;

        ProductCategory^ category = categories[list->SelectedIndices[0]];
        txtId->Text = category->Id;
        txtName->Text = category->Name;
        txtAcronym->Text = category->Acronym;
    }

    void OnAdd(Object^ sender, EventArgs^ e)
    {
        try
        {
            service->Add(txtId->Text->Trim(), txtName->Text->Trim(), txtAcronym->Text->Trim());
            SetStatus("Category " + txtId->Text->Trim() + " added.");
            Reload();
            txtId->Clear();
            txtName->Clear();
            txtAcronym->Clear();
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot add the category", ex->Message);
        }
    }

    // Only the name and the acronym change; the id is part of the barcodes
    void OnUpdate(Object^ sender, EventArgs^ e)
    {
        try
        {
            service->Update(txtId->Text->Trim(), txtName->Text->Trim(), txtAcronym->Text->Trim());
            SetStatus("Category " + txtId->Text->Trim() + " updated.");
            Reload();
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot update the category", ex->Message);
        }
    }

    void OnBack(Object^ sender, EventArgs^ e)
    {
        this->Close();
    }
};

// ---------------------------------------------------------------------------
// Add Product (generates the barcode) and edit a product
// ---------------------------------------------------------------------------
public ref class ProductEditForm : public StatusForm
{
private:
    IProductManager^ manager;
    Barcode^ barcode;
    Product^ editing;                       // nullptr when a new product is added
    List<ProductCategory^>^ categories;

    ComboBox^ cmbCategory;
    TextBox^ txtName;
    TextBox^ txtAcronym;
    ComboBox^ cmbUnit;
    NumericUpDown^ numPrice;
    NumericUpDown^ numTax;
    NumericUpDown^ numDiscount;
    NumericUpDown^ numQuantity;
    PictureBox^ pic;
    Label^ lblBarcode;

public:
    ProductEditForm(IProductManager^ manager, ICategoryService^ categoryService, Barcode^ barcode, Product^ editing)
        : StatusForm("Fill in the product and press Save: the barcode is generated from the category.")
    {
        this->manager = manager;
        this->barcode = barcode;
        this->editing = editing;
        categories = categoryService->All();
        BuildUi();

        if (editing == nullptr && categories->Count == 0)
            SetStatus("There are no product categories yet: add a category first (Categories).");
        if (editing != nullptr)
            LoadProduct();
    }

private:
    void BuildUi()
    {
        if (editing == nullptr)
            this->Text = "Add Product";
        else
            this->Text = "Edit Product";
        this->ClientSize = Drawing::Size(760, 420);
        this->StartPosition = FormStartPosition::CenterScreen;

        array<String^>^ names = gcnew array<String^>(categories->Count);
        for (int i = 0; i < categories->Count; i++)
            names[i] = categories[i]->Id + " - " + categories[i]->Name + " (" + categories[i]->Acronym + ")";

        cmbCategory = Ui::MakeCombo(130, 14, 300, names);
        txtName = Ui::MakeTextBox(130, 48, 300);
        txtAcronym = Ui::MakeTextBox(130, 82, 150);
        txtAcronym->MaxLength = Product::MaxAcronymLength;
        cmbUnit = Ui::MakeCombo(130, 116, 150, manager->UnitNames());
        numPrice = Ui::MakeNumber(130, 150, 150, 0, 10000000, 2, 0);
        numTax = Ui::MakeNumber(130, 184, 150, 0, 10000000, 2, 0);
        numDiscount = Ui::MakeNumber(130, 218, 150, 0, 10000000, 2, 0);
        numQuantity = Ui::MakeNumber(130, 252, 150, 0, 10000000, 2, 0);

        pic = gcnew PictureBox();
        pic->Location = Point(450, 14);
        pic->Size = Drawing::Size(296, 160);
        pic->BorderStyle = BorderStyle::FixedSingle;
        pic->BackColor = Color::White;
        pic->SizeMode = PictureBoxSizeMode::Zoom;

        lblBarcode = Ui::MakeLabel("", 450, 182);
        lblBarcode->Font = gcnew Drawing::Font("Segoe UI", 12.0f, FontStyle::Bold);

        Button^ btnSave = Ui::MakeButton("Save", 130, 300, 140, 36, gcnew EventHandler(this, &ProductEditForm::OnSave));
        Button^ btnBack = Ui::MakeButton("Back", 290, 300, 90, 36, gcnew EventHandler(this, &ProductEditForm::OnBack));
        if (editing == nullptr)
            btnSave->Text = "Generate Barcode && Save";

        this->Controls->Add(Ui::MakeLabel("Category:", 12, 18));
        this->Controls->Add(cmbCategory);
        this->Controls->Add(Ui::MakeLabel("Name:", 12, 52));
        this->Controls->Add(txtName);
        this->Controls->Add(Ui::MakeLabel("Acronym (max 8):", 12, 86));
        this->Controls->Add(txtAcronym);
        this->Controls->Add(Ui::MakeLabel("Unit:", 12, 120));
        this->Controls->Add(cmbUnit);
        this->Controls->Add(Ui::MakeLabel("Unit price:", 12, 154));
        this->Controls->Add(numPrice);
        this->Controls->Add(Ui::MakeLabel("Tax per unit:", 12, 188));
        this->Controls->Add(numTax);
        this->Controls->Add(Ui::MakeLabel("Discount per unit:", 12, 222));
        this->Controls->Add(numDiscount);
        this->Controls->Add(Ui::MakeLabel("Quantity:", 12, 256));
        this->Controls->Add(numQuantity);
        this->Controls->Add(pic);
        this->Controls->Add(lblBarcode);
        this->Controls->Add(btnSave);
        this->Controls->Add(btnBack);

        this->CancelButton = btnBack;
    }

    // Edit mode: the fields show the product; barcode and category cannot change
    void LoadProduct()
    {
        for (int i = 0; i < categories->Count; i++)
        {
            if (categories[i]->Id == editing->CategoryId)
                cmbCategory->SelectedIndex = i;
        }
        cmbCategory->Enabled = false;

        txtName->Text = editing->Name;
        txtAcronym->Text = editing->Acronym;
        cmbUnit->SelectedItem = editing->Unit;
        numPrice->Value = Decimal(editing->Price);
        numTax->Value = Decimal(editing->Tax);
        numDiscount->Value = Decimal(editing->Discount);
        numQuantity->Value = Decimal(editing->Quantity);
        lblBarcode->Text = "Barcode " + editing->Barcode;
        ShowBarcode(editing);
    }

    ProductDraft^ ReadDraft()
    {
        String^ categoryId = "";
        if (cmbCategory->SelectedIndex >= 0 && cmbCategory->SelectedIndex < categories->Count)
            categoryId = categories[cmbCategory->SelectedIndex]->Id;

        return gcnew ProductDraft(categoryId, txtAcronym->Text->Trim(), txtName->Text->Trim(), cmbUnit->Text,
            Decimal::ToDouble(numPrice->Value), Decimal::ToDouble(numTax->Value),
            Decimal::ToDouble(numDiscount->Value), Decimal::ToDouble(numQuantity->Value));
    }

    // Draws the barcode of the product and saves it as <barcode>.svg (the product's barcode file)
    void ShowBarcode(Product^ product)
    {
        List<int>^ row = barcode->Encode(product->Barcode);

        Image^ old = pic->Image;
        pic->Image = barcode->Render(row, 2);
        if (old != nullptr) delete old;

        if (editing == nullptr)
            barcode->SaveSvg(row, product->BarcodeFile);
    }

    void ClearFields()
    {
        txtName->Clear();
        txtAcronym->Clear();
        numPrice->Value = Decimal(0);
        numTax->Value = Decimal(0);
        numDiscount->Value = Decimal(0);
        numQuantity->Value = Decimal(0);
        txtName->Focus();
    }

    void OnSave(Object^ sender, EventArgs^ e)
    {
        try
        {
            ProductDraft^ draft = ReadDraft();

            if (editing == nullptr)
            {
                Product^ product = manager->Add(draft);
                lblBarcode->Text = "Barcode " + product->Barcode;
                ShowBarcode(product);
                SetStatus("Product " + product->Acronym + " added. Barcode " + product->Barcode +
                    " saved to " + product->BarcodeFile);
                ClearFields();
            }
            else
            {
                manager->Update(editing, draft);
                SetStatus("Product " + editing->Acronym + " updated.");
            }
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot save the product", ex->Message);
        }
    }

    void OnBack(Object^ sender, EventArgs^ e)
    {
        this->Close();
    }
};

// ---------------------------------------------------------------------------
// Inventory: every product with its available quantity
// ---------------------------------------------------------------------------
public ref class InventoryForm : public StatusForm
{
private:
    IProductManager^ manager;
    ICategoryService^ categoryService;
    Barcode^ barcode;
    List<Product^>^ products;
    ListView^ list;

public:
    InventoryForm(IProductManager^ manager, ICategoryService^ categoryService, Barcode^ barcode)
        : StatusForm("Select a product, then update its quantity or edit it.")
    {
        this->manager = manager;
        this->categoryService = categoryService;
        this->barcode = barcode;
        BuildUi();
        Reload();
    }

private:
    void BuildUi()
    {
        this->Text = "Products and Inventory";
        this->ClientSize = Drawing::Size(900, 480);
        this->StartPosition = FormStartPosition::CenterScreen;

        list = Ui::MakeList(12, 12, 876, 380,
            gcnew array<String^>{ "Id", "Barcode", "Cat", "Item", "Name", "Unit", "Price", "Available" },
            gcnew array<int>{ 50, 90, 50, 90, 280, 70, 100, 110 });
        list->DoubleClick += gcnew EventHandler(this, &InventoryForm::OnQuantity);

        Button^ btnBack = Ui::MakeButton("Back", 798, 410, 90, 32, gcnew EventHandler(this, &InventoryForm::OnBack));

        this->Controls->Add(list);
        this->Controls->Add(Ui::MakeButton("Update Quantity", 12, 410, 150, 32, gcnew EventHandler(this, &InventoryForm::OnQuantity)));
        this->Controls->Add(Ui::MakeButton("Edit Product...", 172, 410, 150, 32, gcnew EventHandler(this, &InventoryForm::OnEdit)));
        this->Controls->Add(btnBack);

        this->CancelButton = btnBack;
    }

    void Reload()
    {
        int selected = 0;
        if (list->SelectedIndices->Count > 0)
            selected = list->SelectedIndices[0];

        products = manager->All();
        list->Items->Clear();
        for each(Product ^ product in products)
            Ui::AddRow(list, gcnew array<String^>{ product->Id.ToString(), product->Barcode, product->CategoryId,
                product->Acronym, product->Name, product->Unit, Fmt::Money(product->Price), Fmt::Number(product->Quantity) });

        if (selected < list->Items->Count)
            list->Items[selected]->Selected = true;
        SetStatus(Convert::ToString(products->Count) + " products.");
    }

    Product^ SelectedProduct()
    {
        if (list->SelectedIndices->Count == 0)
        {
            SetStatus("Select a product first.");
            return nullptr;
        }
        return products[list->SelectedIndices[0]];
    }

    void OnQuantity(Object^ sender, EventArgs^ e)
    {
        Product^ product = SelectedProduct();
        if (product == nullptr)
            return;

        QuantityForm^ dlg = gcnew QuantityForm("Update inventory",
            product->Acronym + " - " + product->Name + "   (now " + Fmt::Number(product->Quantity) + ")",
            product->Quantity, 0, 10000000);
        System::Windows::Forms::DialogResult result = dlg->ShowDialog();
        double quantity = dlg->Quantity;
        delete dlg;

        if (result != System::Windows::Forms::DialogResult::OK)
            return;

        try
        {
            manager->SetQuantity(product, quantity);
            Reload();
            SetStatus("Quantity of " + product->Acronym + " is now " + Fmt::Number(quantity));
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot update the inventory", ex->Message);
        }
    }

    void OnEdit(Object^ sender, EventArgs^ e)
    {
        Product^ product = SelectedProduct();
        if (product == nullptr)
            return;

        try
        {
            ProductEditForm^ form = gcnew ProductEditForm(manager, categoryService, barcode, product);
            form->ShowDialog();
            delete form;
            Reload();
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot edit the product", ex->Message);
        }
    }

    void OnBack(Object^ sender, EventArgs^ e)
    {
        this->Close();
    }
};

// ---------------------------------------------------------------------------
// Seasonal discounts (Seasons.txt)
// ---------------------------------------------------------------------------
public ref class SeasonForm : public StatusForm
{
private:
    ISeasonService^ service;
    List<Season^>^ seasons;
    ListView^ list;
    DateTimePicker^ dtpStart;
    DateTimePicker^ dtpEnd;
    NumericUpDown^ numPercent;

public:
    SeasonForm(ISeasonService^ service)
        : StatusForm("A bill gets the discount of the active season that covers the date of the bill.")
    {
        this->service = service;
        BuildUi();
        Reload();
    }

private:
    void BuildUi()
    {
        this->Text = "Seasons and Discounts";
        this->ClientSize = Drawing::Size(620, 420);
        this->StartPosition = FormStartPosition::CenterScreen;

        list = Ui::MakeList(12, 12, 596, 240,
            gcnew array<String^>{ "Start date", "End date", "Discount %", "Active" },
            gcnew array<int>{ 140, 140, 130, 120 });

        dtpStart = Ui::MakeDatePicker(110, 270, 140);
        dtpEnd = Ui::MakeDatePicker(110, 304, 140);
        numPercent = Ui::MakeNumber(110, 338, 90, 0.5, 100, 1, 10);

        Button^ btnBack = Ui::MakeButton("Back", 518, 372, 90, 32, gcnew EventHandler(this, &SeasonForm::OnBack));

        this->Controls->Add(list);
        this->Controls->Add(Ui::MakeLabel("Start date:", 12, 274));
        this->Controls->Add(dtpStart);
        this->Controls->Add(Ui::MakeLabel("End date:", 12, 308));
        this->Controls->Add(dtpEnd);
        this->Controls->Add(Ui::MakeLabel("Discount %:", 12, 342));
        this->Controls->Add(numPercent);
        this->Controls->Add(Ui::MakeButton("Add Season", 290, 270, 160, 32, gcnew EventHandler(this, &SeasonForm::OnAdd)));
        this->Controls->Add(Ui::MakeButton("Activate / Deactivate", 290, 310, 160, 32, gcnew EventHandler(this, &SeasonForm::OnToggle)));
        this->Controls->Add(btnBack);

        this->CancelButton = btnBack;
    }

    void Reload()
    {
        seasons = service->All();
        list->Items->Clear();
        for each(Season ^ season in seasons)
        {
            String^ active = "false";
            if (season->Active)
                active = "true";
            Ui::AddRow(list, gcnew array<String^>{ Fmt::DateText(season->Start), Fmt::DateText(season->End),
                Fmt::Number(season->Percentage), active });
        }
    }

    void OnAdd(Object^ sender, EventArgs^ e)
    {
        try
        {
            service->Add(dtpStart->Value, dtpEnd->Value, Decimal::ToDouble(numPercent->Value));
            Reload();
            SetStatus("Season added (active).");
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot add the season", ex->Message);
        }
    }

    void OnToggle(Object^ sender, EventArgs^ e)
    {
        if (list->SelectedIndices->Count == 0)
        {
            SetStatus("Select a season first.");
            return;
        }

        try
        {
            int index = list->SelectedIndices[0];
            bool active = !seasons[index]->Active;
            service->SetActive(index, active);
            Reload();
            list->Items[index]->Selected = true;
            if (active)
                SetStatus("Season activated.");
            else
                SetStatus("Season deactivated.");
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot change the season", ex->Message);
        }
    }

    void OnBack(Object^ sender, EventArgs^ e)
    {
        this->Close();
    }
};

// ---------------------------------------------------------------------------
// Approvals: paid orders that a cashier wants to edit
// ---------------------------------------------------------------------------
public ref class ApprovalForm : public StatusForm
{
private:
    IApprovalDesk^ desk;
    List<Order^>^ pending;
    ListView^ list;

public:
    ApprovalForm(IApprovalDesk^ desk)
        : StatusForm("Approve or reject the edit of a paid order (only within 7 days of the payment).")
    {
        this->desk = desk;
        BuildUi();
        Reload();
    }

private:
    void BuildUi()
    {
        this->Text = "Edit Approvals";
        this->ClientSize = Drawing::Size(660, 400);
        this->StartPosition = FormStartPosition::CenterScreen;

        list = Ui::MakeList(12, 12, 636, 300,
            gcnew array<String^>{ "Order Id", "Date", "Customer", "Payment", "Amount" },
            gcnew array<int>{ 90, 90, 230, 100, 110 });

        Button^ btnBack = Ui::MakeButton("Back", 558, 324, 90, 32, gcnew EventHandler(this, &ApprovalForm::OnBack));

        this->Controls->Add(list);
        this->Controls->Add(Ui::MakeButton("Approve", 12, 324, 110, 32, gcnew EventHandler(this, &ApprovalForm::OnApprove)));
        this->Controls->Add(Ui::MakeButton("Reject", 132, 324, 110, 32, gcnew EventHandler(this, &ApprovalForm::OnReject)));
        this->Controls->Add(btnBack);

        this->CancelButton = btnBack;
    }

    void Reload()
    {
        pending = desk->Pending();
        list->Items->Clear();
        for each(Order ^ order in pending)
            Ui::AddRow(list, gcnew array<String^>{ order->Id, order->Date, order->CustomerName,
                order->PaymentMode, Fmt::Money(order->Payment) });

        if (list->Items->Count > 0)
            list->Items[0]->Selected = true;
        SetStatus(Convert::ToString(pending->Count) + " request(s) waiting.");
    }

    void Answer(bool approve)
    {
        if (list->SelectedIndices->Count == 0)
        {
            SetStatus("Select a request first.");
            return;
        }

        try
        {
            String^ orderId = pending[list->SelectedIndices[0]]->Id;
            if (approve)
                desk->Approve(orderId);
            else
                desk->Reject(orderId);

            Reload();
            if (approve)
                SetStatus("Order " + orderId + " approved: the cashier can edit it now.");
            else
                SetStatus("Order " + orderId + " rejected.");
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot answer the request", ex->Message);
        }
    }

    void OnApprove(Object^ sender, EventArgs^ e)
    {
        Answer(true);
    }

    void OnReject(Object^ sender, EventArgs^ e)
    {
        Answer(false);
    }

    void OnBack(Object^ sender, EventArgs^ e)
    {
        this->Close();
    }
};

// ---------------------------------------------------------------------------
// A report as fixed-width text (used for the PDF)
// ---------------------------------------------------------------------------
public ref class ReportText abstract sealed
{
private:
    static String^ Line(array<String^>^ cells, array<int>^ widths)
    {
        StringBuilder^ sb = gcnew StringBuilder();
        for (int c = 0; c < cells->Length; c++)
            sb->Append(Fmt::Left(cells[c], widths[c] + 2));
        return sb->ToString()->TrimEnd();
    }

public:
    static String^ Render(ReportTable^ table)
    {
        int columns = table->Headers->Length;
        array<int>^ widths = gcnew array<int>(columns);
        int total = 0;

        for (int c = 0; c < columns; c++)
            widths[c] = table->Headers[c]->Length;
        for each(array<String^> ^ row in table->Rows)
        {
            for (int c = 0; c < columns; c++)
            {
                if (row[c]->Length > widths[c])
                    widths[c] = row[c]->Length;
            }
        }
        for (int c = 0; c < columns; c++)
            total += widths[c] + 2;

        StringBuilder^ sb = gcnew StringBuilder();
        sb->AppendLine(table->Title);
        sb->AppendLine("");
        sb->AppendLine(Line(table->Headers, widths));
        sb->AppendLine(gcnew String(L'-', total));
        for each(array<String^> ^ row in table->Rows)
            sb->AppendLine(Line(row, widths));
        sb->AppendLine("");
        sb->AppendLine(table->Footer);
        return sb->ToString();
    }
};

// ---------------------------------------------------------------------------
// Reports (manager): the table on the screen, "Export PDF" for printing
// ---------------------------------------------------------------------------
public ref class ReportForm : public StatusForm
{
private:
    IReportService^ service;
    IPdfExporter^ exporter;
    ReportTable^ shown;

    ComboBox^ cmbReport;
    ComboBox^ cmbPeriod;
    DateTimePicker^ dtpFrom;
    DateTimePicker^ dtpTo;
    ListView^ list;
    Label^ lblFooter;

public:
    ReportForm(IReportService^ service, IPdfExporter^ exporter)
        : StatusForm("Choose a report and press Show.")
    {
        this->service = service;
        this->exporter = exporter;
        shown = nullptr;
        BuildUi();
        ApplyPeriod();
    }

private:
    void BuildUi()
    {
        this->Text = "Reports";
        this->ClientSize = Drawing::Size(920, 590);
        this->StartPosition = FormStartPosition::CenterScreen;

        cmbReport = Ui::MakeCombo(70, 12, 270, service->Names());
        cmbReport->SelectedIndexChanged += gcnew EventHandler(this, &ReportForm::OnChoice);
        cmbPeriod = Ui::MakeCombo(420, 12, 130, ReportPeriod::Names());
        cmbPeriod->SelectedIndexChanged += gcnew EventHandler(this, &ReportForm::OnChoice);
        dtpFrom = Ui::MakeDatePicker(620, 12, 120);
        dtpTo = Ui::MakeDatePicker(780, 12, 120);

        list = gcnew ListView();
        list->Location = Point(12, 48);
        list->Size = Drawing::Size(896, 440);
        list->View = View::Details;
        list->FullRowSelect = true;
        list->GridLines = true;

        lblFooter = Ui::MakeLabel("", 12, 498);
        lblFooter->Font = gcnew Drawing::Font("Segoe UI", 10.0f, FontStyle::Bold);

        Button^ btnBack = Ui::MakeButton("Back", 808, 530, 100, 32, gcnew EventHandler(this, &ReportForm::OnBack));

        this->Controls->Add(Ui::MakeLabel("Report:", 12, 16));
        this->Controls->Add(cmbReport);
        this->Controls->Add(Ui::MakeLabel("Period:", 360, 16));
        this->Controls->Add(cmbPeriod);
        this->Controls->Add(Ui::MakeLabel("From:", 580, 16));
        this->Controls->Add(dtpFrom);
        this->Controls->Add(Ui::MakeLabel("To:", 750, 16));
        this->Controls->Add(dtpTo);
        this->Controls->Add(list);
        this->Controls->Add(lblFooter);
        this->Controls->Add(Ui::MakeButton("Show", 12, 530, 110, 32, gcnew EventHandler(this, &ReportForm::OnShow)));
        this->Controls->Add(Ui::MakeButton("Export PDF...", 132, 530, 130, 32, gcnew EventHandler(this, &ReportForm::OnExport)));
        this->Controls->Add(btnBack);

        this->CancelButton = btnBack;
    }

    // Only the cash-sales and all-sales reports use the period; Custom unlocks the two dates
    void ApplyPeriod()
    {
        bool usesPeriod = service->UsesPeriod(cmbReport->Text);
        cmbPeriod->Enabled = usesPeriod;

        DateTime from = dtpFrom->Value;
        DateTime to = dtpTo->Value;
        ReportPeriod::Range(cmbPeriod->Text, DateTime::Today, from, to);
        dtpFrom->Value = from;
        dtpTo->Value = to;

        bool custom = usesPeriod && cmbPeriod->Text == ReportPeriod::Custom;
        dtpFrom->Enabled = custom;
        dtpTo->Enabled = custom;
    }

    void OnChoice(Object^ sender, EventArgs^ e)
    {
        ApplyPeriod();
    }

    // The table fills the list: the columns follow the report
    void Fill(ReportTable^ table)
    {
        list->Items->Clear();
        list->Columns->Clear();

        int width = (list->Width - 30) / table->Headers->Length;
        for each(String ^ header in table->Headers)
            list->Columns->Add(header, width);
        for each(array<String^> ^ row in table->Rows)
            Ui::AddRow(list, row);

        lblFooter->Text = table->Footer;
    }

    void OnShow(Object^ sender, EventArgs^ e)
    {
        try
        {
            shown = service->Build(cmbReport->Text, dtpFrom->Value, dtpTo->Value);
            Fill(shown);
            SetStatus(shown->Title);
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot build the report", ex->Message);
        }
    }

    void OnExport(Object^ sender, EventArgs^ e)
    {
        if (shown == nullptr)
        {
            SetStatus("Show a report first.");
            return;
        }

        try
        {
            SaveFileDialog^ dlg = gcnew SaveFileDialog();
            dlg->Filter = "PDF file (*.pdf)|*.pdf";
            dlg->FileName = "Report_" + DateTime::Now.ToString("yyyyMMdd_HHmm", CultureInfo::InvariantCulture);
            dlg->InitialDirectory = Files::Folder;
            dlg->DefaultExt = "pdf";
            dlg->AddExtension = true;

            if (dlg->ShowDialog() != System::Windows::Forms::DialogResult::OK)
                return;

            exporter->Export(dlg->FileName, ReportText::Render(shown));
            SetStatus("Report saved as " + dlg->FileName);
        }
        catch (Exception^ ex)
        {
            Ui::Warn("Cannot save the PDF", ex->Message);
        }
    }

    void OnBack(Object^ sender, EventArgs^ e)
    {
        this->Close();
    }
};
