#pragma once

// =============================================================================
// ManagerServices.h - what only the manager does (no files, no screens in here)
//   CategoryService   product categories (PCategory.txt)
//   ProductService    add / edit products, barcode numbering, inventory updates
//   SeasonService     seasonal discounts (Seasons.txt)
//   ReportService     the six reports in table form
//   Depends on        Common.h, Domain.h (interfaces only)
// =============================================================================

#include "Common.h"
#include "Domain.h"

// ---------------------------------------------------------------------------
// Product categories
// ---------------------------------------------------------------------------
public interface class ICategoryService
{
    List<ProductCategory^>^ All();
    void Add(String^ id, String^ name, String^ acronym);
    void Update(String^ id, String^ name, String^ acronym);
};

public ref class CategoryService : public ICategoryService
{
private:
    ICategoryRepository^ categories;

    void ValidateText(String^ name, String^ acronym)
    {
        if (String::IsNullOrEmpty(name))
            throw gcnew ArgumentException("Enter the category name.");

        if (String::IsNullOrEmpty(acronym) || acronym->Contains(" "))
            throw gcnew ArgumentException("Enter the category acronym (one word).");

        if (name->IndexOf(L'#') >= 0 || acronym->IndexOf(L'#') >= 0)
            throw gcnew ArgumentException("The '#' character cannot be used.");
    }

public:
    CategoryService(ICategoryRepository^ categories)
    {
        this->categories = categories;
    }

    virtual List<ProductCategory^>^ All()
    {
        return categories->FindAll();
    }

    virtual void Add(String^ id, String^ name, String^ acronym)
    {
        if (!ProductCategory::IsValidId(id))
            throw gcnew ArgumentException("The category id must be 2 digits, from 10 to 99.");

        ValidateText(name, acronym);

        if (categories->FindById(id) != nullptr)
            throw gcnew ArgumentException("Category " + id + " already exists.");

        categories->Add(gcnew ProductCategory(id, name, acronym));
    }

    // The id never changes (it is part of the barcodes), only name and acronym
    virtual void Update(String^ id, String^ name, String^ acronym)
    {
        ValidateText(name, acronym);

        if (categories->FindById(id) == nullptr)
            throw gcnew ArgumentException("Category " + id + " does not exist.");

        categories->Update(gcnew ProductCategory(id, name, acronym));
    }
};

// ---------------------------------------------------------------------------
// Products and inventory
// ---------------------------------------------------------------------------

// What the manager typed about a product
public ref class ProductDraft
{
public:
    String^ CategoryId;
    String^ Acronym;
    String^ Name;
    String^ Unit;
    double Price;
    double Tax;
    double Discount;
    double Quantity;

    ProductDraft(String^ categoryId, String^ acronym, String^ name, String^ unit,
        double price, double tax, double discount, double quantity)
    {
        CategoryId = categoryId;
        Acronym = acronym;
        Name = name;
        Unit = unit;
        Price = price;
        Tax = tax;
        Discount = discount;
        Quantity = quantity;
    }
};

public interface class IProductManager
{
    List<Product^>^ All();
    array<String^>^ UnitNames();
    Product^ Add(ProductDraft^ draft);                       // makes the barcode and the id, saves
    void Update(Product^ product, ProductDraft^ draft);      // edits an existing product, saves
    void SetQuantity(Product^ product, double quantity);     // inventory update, saves
};

public ref class ProductService : public IProductManager
{
private:
    literal int SerialDigits = 4;           // barcode = 2 category digits + 4 serial digits

    IProductList^ list;
    IProductAdder^ adder;
    IStockWriter^ writer;
    NameSet^ units;
    ICategoryRepository^ categories;

    void Validate(ProductDraft^ draft)
    {
        if (String::IsNullOrEmpty(draft->Name))
            throw gcnew ArgumentException("Enter the product name.");

        if (String::IsNullOrEmpty(draft->Acronym) || draft->Acronym->Contains(" ") ||
            draft->Acronym->Length > Product::MaxAcronymLength)
            throw gcnew ArgumentException("The acronym must be one word of at most 8 characters.");

        if (draft->Name->IndexOf(L'#') >= 0 || draft->Acronym->IndexOf(L'#') >= 0)
            throw gcnew ArgumentException("The '#' character cannot be used.");

        if (!units->Contains(draft->Unit))
            throw gcnew ArgumentException("Choose the unit: " + String::Join(" or ", units->ToArray()) + ".");

        if (draft->Price < 0 || draft->Tax < 0 || draft->Discount < 0 || draft->Quantity < 0)
            throw gcnew ArgumentException("Price, tax, discount and quantity cannot be negative.");

        if (draft->Discount > draft->Price)
            throw gcnew ArgumentException("The discount cannot be more than the unit price.");
    }

    void Fill(Product^ product, ProductDraft^ draft)
    {
        product->Acronym = draft->Acronym;
        product->Name = draft->Name;
        product->Unit = draft->Unit;
        product->Price = draft->Price;
        product->Tax = draft->Tax;
        product->Discount = draft->Discount;
        product->Quantity = draft->Quantity;
    }

    // The first two digits are the category, the other four are the next free number
    // of that category (highest existing serial + 1)
    String^ NextBarcode(String^ categoryId)
    {
        int highest = 0;
        for each(Product ^ p in list->All())
        {
            if (p->Barcode->Length == 2 + SerialDigits && p->Barcode->StartsWith(categoryId))
            {
                int serial;
                if (Int32::TryParse(p->Barcode->Substring(2), serial) && serial > highest)
                    highest = serial;
            }
        }

        int next = highest + 1;
        if (next > 9999)
            throw gcnew InvalidOperationException("Category " + categoryId + " is full (9999 products).");

        return categoryId + next.ToString()->PadLeft(SerialDigits, L'0');
    }

public:
    ProductService(IProductList^ list, IProductAdder^ adder, IStockWriter^ writer,
        NameSet^ units, ICategoryRepository^ categories)
    {
        this->list = list;
        this->adder = adder;
        this->writer = writer;
        this->units = units;
        this->categories = categories;
    }

    virtual List<Product^>^ All()
    {
        return list->All();
    }

    virtual array<String^>^ UnitNames()
    {
        return units->ToArray();
    }

    virtual Product^ Add(ProductDraft^ draft)
    {
        if (String::IsNullOrEmpty(draft->CategoryId) || categories->FindById(draft->CategoryId) == nullptr)
            throw gcnew ArgumentException("Choose a product category first (Categories screen).");

        Validate(draft);

        Product^ product = gcnew Product();
        product->Id = adder->NextId();
        product->CategoryId = draft->CategoryId;
        product->Barcode = NextBarcode(draft->CategoryId);
        Fill(product, draft);
        product->BarcodeFile = Path::Combine(Files::Folder, product->Barcode + ".svg");

        adder->Add(product);
        return product;
    }

    // The barcode and the category never change
    virtual void Update(Product^ product, ProductDraft^ draft)
    {
        Validate(draft);
        Fill(product, draft);
        writer->Save();
    }

    virtual void SetQuantity(Product^ product, double quantity)
    {
        if (quantity < 0)
            throw gcnew ArgumentException("The quantity cannot be negative.");

        product->Quantity = quantity;
        writer->Save();
    }
};

// ---------------------------------------------------------------------------
// Seasonal discounts
// ---------------------------------------------------------------------------
public interface class ISeasonService
{
    List<Season^>^ All();
    void Add(DateTime start, DateTime end, double percentage);
    void SetActive(int index, bool active);
};

public ref class SeasonService : public ISeasonService
{
private:
    ISeasonRepository^ seasons;

public:
    SeasonService(ISeasonRepository^ seasons)
    {
        this->seasons = seasons;
    }

    virtual List<Season^>^ All()
    {
        return seasons->FindAll();
    }

    // A new season starts active
    virtual void Add(DateTime start, DateTime end, double percentage)
    {
        if (end.Date < start.Date)
            throw gcnew ArgumentException("The end date cannot be before the start date.");

        if (percentage <= 0 || percentage > 100)
            throw gcnew ArgumentException("The discount percentage must be more than 0 and at most 100.");

        seasons->Add(gcnew Season(start.Date, end.Date, percentage, true));
    }

    virtual void SetActive(int index, bool active)
    {
        seasons->SetActive(index, active);
    }
};

// ---------------------------------------------------------------------------
// Reports
// ---------------------------------------------------------------------------

// The periods of the cash-sales and all-sales reports
public ref class ReportPeriod abstract sealed
{
public:
    literal String^ Today = "Today";
    literal String^ Week = "Last 7 days";
    literal String^ Month = "This month";
    literal String^ Year = "This year";
    literal String^ Custom = "Custom period";

    static array<String^>^ Names()
    {
        return gcnew array<String^>{ Today, Week, Month, Year, Custom };
    }

    // First and last day of a named period (Custom leaves the dates as they are)
    static void Range(String^ name, DateTime today, DateTime% from, DateTime% to)
    {
        if (name == Today)
        {
            from = today.Date;
            to = today.Date;
        }
        else if (name == Week)
        {
            from = today.Date.AddDays(-6);
            to = today.Date;
        }
        else if (name == Month)
        {
            from = DateTime(today.Year, today.Month, 1);
            to = today.Date;
        }
        else if (name == Year)
        {
            from = DateTime(today.Year, 1, 1);
            to = today.Date;
        }
    }
};

public interface class IReportService
{
    array<String^>^ Names();                                        // the reports that exist
    bool UsesPeriod(String^ name);                                  // does it need From / To dates?
    ReportTable^ Build(String^ name, DateTime from, DateTime to);
};

public ref class ReportService : public IReportService
{
private:
    literal String^ IncompleteName = "Incomplete orders";
    literal String^ CompletedName = "Completed orders";
    literal String^ CashName = "Cash sales";
    literal String^ SalesName = "All sales";
    literal String^ TopSoldName = "Top 10 most sold items";
    literal String^ TopStockName = "Top 10 items by remaining stock";
    literal int TopCount = 10;

    IOrderRepository^ orders;
    IOrderDetailRepository^ details;
    IBillReader^ bills;
    IProductList^ products;

    // Completed = paid or archived; incomplete = everything else
    bool IsCompleted(Order^ order)
    {
        return order->Status == OrderStatus::Paid || order->Status == OrderStatus::Archived;
    }

    bool InRange(Order^ order, DateTime from, DateTime to)
    {
        DateTime day = Fmt::ParseDate(order->Date);
        return day >= from.Date && day <= to.Date;
    }

    String^ Period(DateTime from, DateTime to)
    {
        return Fmt::DateText(from) + " to " + Fmt::DateText(to);
    }

    Dictionary<String^, bool>^ CompletedIds()
    {
        Dictionary<String^, bool>^ ids = gcnew Dictionary<String^, bool>();
        for each(Order ^ order in orders->FindAll())
        {
            if (IsCompleted(order))
                ids[order->Id] = true;
        }
        return ids;
    }

    // Positions of the 'count' largest values, largest first
    List<int>^ TopIndexes(List<double>^ values, int count)
    {
        List<int>^ positions = gcnew List<int>();
        for (int i = 0; i < values->Count; i++)
            positions->Add(i);

        for (int i = 0; i < positions->Count && i < count; i++)
        {
            int best = i;
            for (int j = i + 1; j < positions->Count; j++)
            {
                if (values[positions[j]] > values[positions[best]])
                    best = j;
            }
            int temp = positions[i];
            positions[i] = positions[best];
            positions[best] = temp;
        }

        if (positions->Count > count)
            positions->RemoveRange(count, positions->Count - count);
        return positions;
    }

    // Incomplete (completed == false) or completed orders with the total amount
    ReportTable^ OrdersReport(bool completed)
    {
        String^ title = "Incomplete orders (not paid or archived)";
        if (completed)
            title = "Completed orders";
        ReportTable^ table = gcnew ReportTable(title,
            gcnew array<String^>{ "Order Id", "Date", "Time", "Customer", "Status", "Amount" });

        double total = 0;
        int count = 0;
        for each(Order ^ order in orders->FindAll())
        {
            if (IsCompleted(order) != completed)
                continue;

            table->AddRow(gcnew array<String^>{ order->Id, order->Date, order->Time,
                order->CustomerName, order->Status, Fmt::Money(order->Payment) });
            total += order->Payment;
            count++;
        }

        table->Footer = Convert::ToString(count) + " orders, total amount PKR " + Fmt::Money(total);
        return table;
    }

    ReportTable^ CashSales(DateTime from, DateTime to)
    {
        ReportTable^ table = gcnew ReportTable("Cash sales " + Period(from, to),
            gcnew array<String^>{ "Order Id", "Date", "Time", "Customer", "Amount" });

        double total = 0;
        int count = 0;
        for each(Order ^ order in orders->FindAll())
        {
            if (!IsCompleted(order) || order->PaymentMode != PaymentNames::Cash || !InRange(order, from, to))
                continue;

            table->AddRow(gcnew array<String^>{ order->Id, order->Date, order->Time,
                order->CustomerName, Fmt::Money(order->Payment) });
            total += order->Payment;
            count++;
        }

        table->Footer = Convert::ToString(count) + " cash sales, total PKR " + Fmt::Money(total);
        return table;
    }

    // All completed orders of the period with their discounts (from the order's last bill)
    ReportTable^ AllSales(DateTime from, DateTime to)
    {
        Dictionary<String^, BillRecord^>^ latest = gcnew Dictionary<String^, BillRecord^>();
        for each(BillRecord ^ bill in bills->FindAll())
            latest[bill->OrderId] = bill;

        ReportTable^ table = gcnew ReportTable("All sales " + Period(from, to),
            gcnew array<String^>{ "Order Id", "Date", "Customer", "Mode", "Bill Id", "Discounts", "Amount" });

        double total = 0;
        double discounts = 0;
        int count = 0;
        for each(Order ^ order in orders->FindAll())
        {
            if (!IsCompleted(order) || !InRange(order, from, to))
                continue;

            double discount = 0;
            String^ billId = "-";
            BillRecord^ bill = nullptr;
            if (latest->TryGetValue(order->Id, bill))
            {
                discount = bill->TotalDiscount + bill->SeasonalDiscount + bill->SpecialDiscount;
                billId = bill->Id;
            }

            table->AddRow(gcnew array<String^>{ order->Id, order->Date, order->CustomerName,
                order->PaymentMode, billId, Fmt::Money(discount), Fmt::Money(order->Payment) });
            total += order->Payment;
            discounts += discount;
            count++;
        }

        table->Footer = Convert::ToString(count) + " sales, total amount PKR " + Fmt::Money(total) +
            ", total discounts PKR " + Fmt::Money(discounts);
        return table;
    }

    // The ten items sold most (completed orders only), with what is left in stock
    ReportTable^ TopSold()
    {
        Dictionary<String^, bool>^ completed = CompletedIds();
        Dictionary<int, double>^ sold = gcnew Dictionary<int, double>();
        for each(OrderDetail ^ detail in details->FindAll())
        {
            if (!completed->ContainsKey(detail->OrderId))
                continue;

            double before = 0;
            sold->TryGetValue(detail->ProductId, before);
            sold[detail->ProductId] = before + detail->Quantity;
        }

        List<Product^>^ items = gcnew List<Product^>();
        List<double>^ quantities = gcnew List<double>();
        for each(Product ^ product in products->All())
        {
            double quantity = 0;
            if (sold->TryGetValue(product->Id, quantity))
            {
                items->Add(product);
                quantities->Add(quantity);
            }
        }

        ReportTable^ table = gcnew ReportTable("Top 10 most sold items",
            gcnew array<String^>{ "Rank", "Barcode", "Item", "Name", "Sold", "Remaining" });

        int rank = 1;
        for each(int i in TopIndexes(quantities, TopCount))
        {
            table->AddRow(gcnew array<String^>{ Convert::ToString(rank), items[i]->Barcode, items[i]->Acronym,
                items[i]->Name, Fmt::Number(quantities[i]), Fmt::Number(items[i]->Quantity) });
            rank++;
        }

        table->Footer = "Sorted by quantity sold, highest first.";
        return table;
    }

    // The ten items with the most stock left: no need to buy these
    ReportTable^ TopStock()
    {
        List<Product^>^ items = products->All();
        List<double>^ quantities = gcnew List<double>();
        for each(Product ^ product in items)
            quantities->Add(product->Quantity);

        ReportTable^ table = gcnew ReportTable("Top 10 items by remaining stock",
            gcnew array<String^>{ "Rank", "Barcode", "Item", "Name", "Remaining" });

        int rank = 1;
        for each(int i in TopIndexes(quantities, TopCount))
        {
            table->AddRow(gcnew array<String^>{ Convert::ToString(rank), items[i]->Barcode,
                items[i]->Acronym, items[i]->Name, Fmt::Number(items[i]->Quantity) });
            rank++;
        }

        table->Footer = "Sorted by remaining stock, highest first: do not start a purchase of these items.";
        return table;
    }

public:
    ReportService(IOrderRepository^ orders, IOrderDetailRepository^ details, IBillReader^ bills,
        IProductList^ products)
    {
        this->orders = orders;
        this->details = details;
        this->bills = bills;
        this->products = products;
    }

    virtual array<String^>^ Names()
    {
        return gcnew array<String^>{ IncompleteName, CompletedName, CashName, SalesName, TopSoldName, TopStockName };
    }

    virtual bool UsesPeriod(String^ name)
    {
        return name == CashName || name == SalesName;
    }

    virtual ReportTable^ Build(String^ name, DateTime from, DateTime to)
    {
        if (name == IncompleteName) return OrdersReport(false);
        if (name == CompletedName) return OrdersReport(true);
        if (name == CashName) return CashSales(from, to);
        if (name == SalesName) return AllSales(from, to);
        if (name == TopSoldName) return TopSold();
        if (name == TopStockName) return TopStock();
        throw gcnew ArgumentException("Unknown report: " + name);
    }
};
