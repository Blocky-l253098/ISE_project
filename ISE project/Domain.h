#pragma once

// =============================================================================
// Domain.h - the business model: products, categories, seasons, carts, orders,
//            bills and the rules that may change (stock rule, pricing, payment)
//   Responsibility : business data and business rules, no files, no screens
//   Depends on     : Common.h
//   Extension      : a new rule / policy = a new class implementing the interface;
//                    nothing in this file is edited
// =============================================================================

#include "Common.h"

// ---------------------------------------------------------------------------
// Product
// ---------------------------------------------------------------------------
public ref class Product : public Record
{
private:
    // Position of the unit field: it is the first allowed unit word (from 'first'
    // on) that is followed by four numbers. This lets the name AND the barcode
    // file path contain spaces.
    static int FindUnit(array<String^>^ f, NameSet^ units, int first)
    {
        for (int i = first; i <= f->Length - 6; i++)
        {
            if (!units->Contains(f[i]))
                continue;

            if (Fmt::IsNumber(f[i + 1]) && Fmt::IsNumber(f[i + 2]) &&
                Fmt::IsNumber(f[i + 3]) && Fmt::IsNumber(f[i + 4]))
                return i;
        }
        return -1;
    }

public:
    literal int MaxAcronymLength = 8;

    int Id;
    String^ Barcode;
    String^ CategoryId;     // the first two digits of the barcode
    String^ Acronym;
    String^ Name;
    String^ Unit;           // one of the allowed units (count / weight)
    double Price;           // per unit, without tax
    double Tax;             // per unit, PKR
    double Discount;        // per unit, PKR
    double Quantity;        // available in stock
    String^ BarcodeFile;

    // The first two digits of a barcode are the category id
    static String^ CategoryOf(String^ barcode)
    {
        if (barcode == nullptr || barcode->Length < 2)
            return "";
        return barcode->Substring(0, 2);
    }

    // New format: "id barcode category acronym name... unit price tax discount quantity file..."
    // Old format (no category field) is still read; the category comes from the barcode.
    static Product^ FromLine(String^ line, NameSet^ units)
    {
        array<String^>^ f = line->Split(gcnew array<Char>{ L' ' }, StringSplitOptions::RemoveEmptyEntries);

        bool hasCategory = f->Length >= 3 && f[1]->Length >= 2 &&
            Regex::IsMatch(f[2], "^[0-9][0-9]$") && f[2] == CategoryOf(f[1]);
        int shift = hasCategory ? 1 : 0;

        if (f->Length < 10 + shift)
            throw gcnew FormatException("Bad product line: " + line);

        int u = FindUnit(f, units, 4 + shift);
        if (u < 0)
            throw gcnew FormatException("Bad product line (unit must be " +
                String::Join(" or ", units->ToArray()) + ", then price, tax, discount, " +
                "quantity and barcode file): " + line);

        Product^ p = gcnew Product();
        p->Id = Int32::Parse(f[0]);
        p->Barcode = f[1];
        p->CategoryId = hasCategory ? f[2] : CategoryOf(f[1]);
        p->Acronym = f[2 + shift];
        p->Name = String::Join(" ", f, 3 + shift, u - 3 - shift);
        p->Unit = f[u];
        p->Price = Fmt::Parse(f[u + 1]);
        p->Tax = Fmt::Parse(f[u + 2]);
        p->Discount = Fmt::Parse(f[u + 3]);
        p->Quantity = Fmt::Parse(f[u + 4]);
        p->BarcodeFile = String::Join(" ", f, u + 5, f->Length - u - 5);

        if (p->Acronym->Length > MaxAcronymLength)
            throw gcnew FormatException("Acronym is longer than 8 characters: " + p->Acronym);

        if (p->Price < 0 || p->Tax < 0 || p->Discount < 0 || p->Quantity < 0)
            throw gcnew FormatException("Price, tax, discount and quantity cannot be negative: " + line);

        return p;
    }

    virtual String^ ToLine() override
    {
        return String::Join(" ", gcnew array<String^>
        {
            Id.ToString(), Barcode, CategoryId, Acronym, Name, Unit, Fmt::Number(Price),
                Fmt::Number(Tax), Fmt::Number(Discount), Fmt::Number(Quantity), BarcodeFile
        });
    }
};

// Reading products (small interface: the screens and the workflow only look products up)
public interface class IProductLookup
{
    Product^ FindByBarcode(String^ barcode);     // nullptr when unknown
    Product^ FindById(int id);                   // nullptr when unknown
    List<Product^>^ Search(String^ text);        // barcode, acronym or name contains the text
    String^ Summary();                           // short text about what was loaded
};

// The whole product list (inventory screen, reports, barcode numbering)
public interface class IProductList
{
    List<Product^>^ All();
};

// Adding a product (the manager's Add Product screen)
public interface class IProductAdder
{
    void Add(Product^ product);                  // adds and saves
    int NextId();
};

// Writing the stock back (separate interface: the checkout and the inventory need it)
public interface class IStockWriter
{
    void Save();
};

// ---------------------------------------------------------------------------
// Product category (PCategory.txt): the first two barcode digits
// ---------------------------------------------------------------------------
public ref class ProductCategory : public Record
{
public:
    String^ Id;             // 2 digits, 10 to 99
    String^ Name;
    String^ Acronym;

    ProductCategory(String^ id, String^ name, String^ acronym)
    {
        Id = id;
        Name = name;
        Acronym = acronym;
    }

    // Two digits, the first one not 0 (a barcode cannot start with 0)
    static bool IsValidId(String^ id)
    {
        return Regex::IsMatch(id, "^[1-9][0-9]$");
    }

    // "id name... acronym": the name may contain spaces
    static ProductCategory^ FromLine(String^ line)
    {
        array<String^>^ f = line->Split(gcnew array<Char>{ L' ' }, StringSplitOptions::RemoveEmptyEntries);
        if (f->Length < 3)
            throw gcnew FormatException("Bad category line: " + line);

        return gcnew ProductCategory(f[0], String::Join(" ", f, 1, f->Length - 2), f[f->Length - 1]);
    }

    virtual String^ ToLine() override
    {
        return Id + " " + Name + " " + Acronym;
    }
};

public interface class ICategoryRepository
{
    List<ProductCategory^>^ FindAll();
    ProductCategory^ FindById(String^ id);       // nullptr when unknown
    void Add(ProductCategory^ category);
    void Update(ProductCategory^ category);      // same id, new name / acronym
};

// ---------------------------------------------------------------------------
// Seasonal discount (Seasons.txt)
// ---------------------------------------------------------------------------
public ref class Season : public Record
{
public:
    DateTime Start;
    DateTime End;
    double Percentage;
    bool Active;

    Season(DateTime start, DateTime end, double percentage, bool active)
    {
        Start = start;
        End = end;
        Percentage = percentage;
        Active = active;
    }

    // Is the season active and is this date between its start and end date?
    bool Covers(DateTime when)
    {
        return Active && when.Date >= Start.Date && when.Date <= End.Date;
    }

    // "startDate endDate percentage isActive"
    static Season^ FromLine(String^ line)
    {
        array<String^>^ f = line->Split(gcnew array<Char>{ L' ' }, StringSplitOptions::RemoveEmptyEntries);
        if (f->Length != 4)
            throw gcnew FormatException("Bad season line: " + line);

        return gcnew Season(Fmt::ParseDate(f[0]), Fmt::ParseDate(f[1]), Fmt::Parse(f[2]), f[3] == "true");
    }

    virtual String^ ToLine() override
    {
        String^ flag = "false";
        if (Active)
            flag = "true";

        return String::Join(" ", gcnew array<String^>
        {
            Fmt::DateText(Start), Fmt::DateText(End), Fmt::Number(Percentage), flag
        });
    }
};

public interface class ISeasonRepository
{
    List<Season^>^ FindAll();
    void Add(Season^ season);
    void SetActive(int index, bool active);      // index = position in the list
};

// The seasonal discount percentage on a date (0 when no season applies)
public interface class ISeasonalDiscount
{
    double PercentageOn(DateTime when);
};

// ---------------------------------------------------------------------------
// Rules that may change
// ---------------------------------------------------------------------------

// Fixed business limits
public ref class DiscountRules abstract sealed
{
public:
    literal double MaxSpecialPercent = 8;       // special discount: not more than 8 percent
};

// May this quantity be sold?
public interface class IStockRule
{
    bool Allows(double wanted, double available);
};

// Requirement text: "added if the required quantity is LESS THAN the available quantity"
public ref class LessThanAvailableRule : public IStockRule
{
public:
    virtual bool Allows(double wanted, double available)
    {
        return wanted < available;
    }
};

// Alternative: the last unit may be sold too
public ref class UpToAvailableRule : public IStockRule
{
public:
    virtual bool Allows(double wanted, double available)
    {
        return wanted <= available;
    }
};

// How much does a quantity of a product cost, tax and discount?
public interface class IPricingPolicy
{
    double Subtotal(Product^ product, double quantity);     // without tax
    double Tax(Product^ product, double quantity);
    double Discount(Product^ product, double quantity);
};

// Prices come straight from Product.txt: per-unit price, tax and discount x quantity
public ref class ProductFilePricing : public IPricingPolicy
{
public:
    virtual double Subtotal(Product^ product, double quantity) { return product->Price * quantity; }
    virtual double Tax(Product^ product, double quantity) { return product->Tax * quantity; }
    virtual double Discount(Product^ product, double quantity) { return product->Discount * quantity; }
};

// One way to pay (cash, card, wallet ...)
public ref class PaymentMethod
{
public:
    String^ Name;
    bool NeedsMerchant;     // card / wallet payments go through ABL, JazzCash ...

    PaymentMethod(String^ name, bool needsMerchant)
    {
        Name = name;
        NeedsMerchant = needsMerchant;
    }
};

// The payment methods the shop accepts. A new method is added by whoever builds the
// catalog (see PosFactory); this class never changes.
public ref class PaymentCatalog
{
private:
    List<PaymentMethod^>^ methods;

public:
    PaymentCatalog()
    {
        methods = gcnew List<PaymentMethod^>();
    }

    void Add(PaymentMethod^ method)
    {
        methods->Add(method);
    }

    array<String^>^ Names()
    {
        List<String^>^ names = gcnew List<String^>();
        for each(PaymentMethod ^ method in methods)
            names->Add(method->Name);
        return names->ToArray();
    }

    // nullptr when the name is not accepted
    PaymentMethod^ Find(String^ name)
    {
        for each(PaymentMethod ^ method in methods)
        {
            if (method->Name == name)
                return method;
        }
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// Order model
// ---------------------------------------------------------------------------

// One product and its quantity inside the order being recorded
public ref class OrderItem
{
public:
    Product^ Item;
    double Quantity;

    OrderItem(Product^ item, double quantity)
    {
        Item = item;
        Quantity = quantity;
    }
};

// The products of the order being recorded + its totals.
// When a PAID order is edited, its old quantities are the "baseline": they are
// already taken from the stock, so the stock rule may count them as available.
public ref class Cart
{
private:
    IStockRule^ rule;
    IPricingPolicy^ pricing;
    List<OrderItem^>^ items;
    List<OrderItem^>^ baseline;

    OrderItem^ Find(Product^ product)
    {
        for each(OrderItem ^ line in items)
        {
            if (line->Item == product)
                return line;
        }
        return nullptr;
    }

    double BaselineOf(Product^ product)
    {
        for each(OrderItem ^ line in baseline)
        {
            if (line->Item == product)
                return line->Quantity;
        }
        return 0;
    }

public:
    Cart(IStockRule^ rule, IPricingPolicy^ pricing)
    {
        this->rule = rule;
        this->pricing = pricing;
        items = gcnew List<OrderItem^>();
        baseline = gcnew List<OrderItem^>();
    }

    property List<OrderItem^>^ Items
    {
        List<OrderItem^> ^ get() { return items; }
    }

    // Remembers a quantity of a paid order that is already out of the stock
    void AddBaseline(Product^ product, double quantity)
    {
        baseline->Add(gcnew OrderItem(product, quantity));
    }

    // false when the stock rule refuses the quantity (nothing is added then)
    bool Add(Product^ product, double quantity)
    {
        OrderItem^ line = Find(product);
        double wanted = quantity + ((line == nullptr) ? 0 : line->Quantity);

        if (!rule->Allows(wanted, product->Quantity + BaselineOf(product)))
            return false;

        if (line == nullptr)
            items->Add(gcnew OrderItem(product, quantity));
        else
            line->Quantity = wanted;
        return true;
    }

    // Takes one line out of the order
    void Remove(OrderItem^ line)
    {
        items->Remove(line);
    }

    // Takes every line out of the order
    void Clear()
    {
        items->Clear();
    }

    // Sets the quantity of a line. false when it is not positive or the stock
    // rule refuses it (nothing is changed then)
    bool SetQuantity(OrderItem^ line, double quantity)
    {
        if (quantity <= 0 || !rule->Allows(quantity, line->Item->Quantity + BaselineOf(line->Item)))
            return false;
        line->Quantity = quantity;
        return true;
    }

    // How much more (or less) of a product leaves the stock because of this cart
    double StockChange(Product^ product)
    {
        double inCart = 0;
        OrderItem^ line = Find(product);
        if (line != nullptr)
            inCart = line->Quantity;
        return inCart - BaselineOf(product);
    }

    // The products whose stock changes: the ones in the cart and in the baseline
    List<Product^>^ AffectedProducts()
    {
        List<Product^>^ list = gcnew List<Product^>();
        for each(OrderItem ^ line in items)
        {
            if (!list->Contains(line->Item))
                list->Add(line->Item);
        }
        for each(OrderItem ^ line in baseline)
        {
            if (!list->Contains(line->Item))
                list->Add(line->Item);
        }
        return list;
    }

    double LineSubtotal(OrderItem^ line) { return pricing->Subtotal(line->Item, line->Quantity); }
    double LineTax(OrderItem^ line) { return pricing->Tax(line->Item, line->Quantity); }
    double LineDiscount(OrderItem^ line) { return pricing->Discount(line->Item, line->Quantity); }

    double TotalWithoutTax()
    {
        double total = 0;
        for each(OrderItem ^ line in items) total += LineSubtotal(line);
        return total;
    }

    double TotalTax()
    {
        double total = 0;
        for each(OrderItem ^ line in items) total += LineTax(line);
        return total;
    }

    // Sum of the discounts on all bought products
    double TotalDiscount()
    {
        double total = 0;
        for each(OrderItem ^ line in items) total += LineDiscount(line);
        return total;
    }

    // Without tax + tax - product discounts (before seasonal / special discount)
    double Payable()
    {
        return TotalWithoutTax() + TotalTax() - TotalDiscount();
    }
};

// What the bill would be: the amount before and after the extra discounts
public ref class PriceQuote
{
public:
    double Base;            // cart payable (product discounts already taken off)
    double Seasonal;        // seasonal discount amount
    double Special;         // special discount amount
    double Payable;         // final amount to pay

    PriceQuote(double baseAmount, double seasonal, double special, double payable)
    {
        Base = baseAmount;
        Seasonal = seasonal;
        Special = special;
        Payable = payable;
    }
};

// What the cashier typed about the customer and the payment
public ref class OrderDraft
{
public:
    String^ CustomerName;
    String^ CustomerContact;
    String^ PaymentMode;
    String^ Merchant;
    double SpecialPercent;      // 0 .. DiscountRules::MaxSpecialPercent
    double AmountReceived;      // negative = exactly the payable amount

    OrderDraft(String^ customerName, String^ customerContact, String^ paymentMode,
        String^ merchant, double specialPercent, double amountReceived)
    {
        CustomerName = customerName;
        CustomerContact = customerContact;
        PaymentMode = paymentMode;
        Merchant = merchant;
        SpecialPercent = specialPercent;
        AmountReceived = amountReceived;
    }
};

// One line of Order.txt
public ref class Order : public Record
{
public:
    String^ Id;             // 6 digit running number + 2 digit day of the order
    String^ Date;
    String^ Time;
    String^ CustomerName;
    String^ CustomerContact;
    String^ Status;         // see OrderStatus
    double Payment;
    String^ PaymentMode;
    String^ Merchant;       // ABL, JazzCash ...

    // A copy, so a failed payment never leaves the original half changed
    Order^ Clone()
    {
        Order^ o = gcnew Order();
        o->Id = Id;
        o->Date = Date;
        o->Time = Time;
        o->CustomerName = CustomerName;
        o->CustomerContact = CustomerContact;
        o->Status = Status;
        o->Payment = Payment;
        o->PaymentMode = PaymentMode;
        o->Merchant = Merchant;
        return o;
    }

    static Order^ FromLine(String^ line)
    {
        array<String^>^ f = line->Split(gcnew array<Char>{ L' ' }, StringSplitOptions::RemoveEmptyEntries);
        if (f->Length != 9)
            throw gcnew FormatException("Bad order line: " + line);

        Order^ o = gcnew Order();
        o->Id = f[0];
        o->Date = f[1];
        o->Time = f[2];
        o->CustomerName = Fmt::Unpack(f[3]);
        o->CustomerContact = Fmt::Unpack(f[4]);
        o->Status = f[5];
        o->Payment = Fmt::Parse(f[6]);
        o->PaymentMode = f[7];
        o->Merchant = Fmt::Unpack(f[8]);
        return o;
    }

    virtual String^ ToLine() override
    {
        return String::Join(" ", gcnew array<String^>
        {
            Id, Date, Time, Fmt::Pack(CustomerName), Fmt::Pack(CustomerContact),
                Status, Fmt::Money(Payment), PaymentMode, Fmt::Pack(Merchant)
        });
    }
};

// One line of OrderDetail.txt
public ref class OrderDetail : public Record
{
public:
    String^ OrderId;
    int ProductId;
    String^ Barcode;
    double Quantity;

    OrderDetail(String^ orderId, int productId, String^ barcode, double quantity)
    {
        OrderId = orderId;
        ProductId = productId;
        Barcode = barcode;
        Quantity = quantity;
    }

    static OrderDetail^ FromLine(String^ line)
    {
        array<String^>^ f = line->Split(gcnew array<Char>{ L' ' }, StringSplitOptions::RemoveEmptyEntries);
        if (f->Length != 4)
            throw gcnew FormatException("Bad order detail line: " + line);

        return gcnew OrderDetail(f[0], Int32::Parse(f[1]), f[2], Fmt::Parse(f[3]));
    }

    virtual String^ ToLine() override
    {
        return String::Join(" ", gcnew array<String^>
        {
            OrderId, ProductId.ToString(), Barcode, Fmt::Number(Quantity)
        });
    }
};

// The states of a manager's approval to edit a paid order
public ref class ApprovalState abstract sealed
{
public:
    literal String^ None = "none";
    literal String^ Pending = "pending";
    literal String^ Approved = "approved";
    literal String^ Rejected = "rejected";
    literal String^ Used = "used";
};

// One line of Approval.txt: a cashier's request to edit a paid order
public ref class EditApproval : public Record
{
public:
    String^ OrderId;
    String^ Date;
    String^ Time;
    String^ State;          // see ApprovalState

    EditApproval(String^ orderId, String^ date, String^ time, String^ state)
    {
        OrderId = orderId;
        Date = date;
        Time = time;
        State = state;
    }

    static EditApproval^ FromLine(String^ line)
    {
        array<String^>^ f = line->Split(gcnew array<Char>{ L' ' }, StringSplitOptions::RemoveEmptyEntries);
        if (f->Length != 4)
            throw gcnew FormatException("Bad approval line: " + line);

        return gcnew EditApproval(f[0], f[1], f[2], f[3]);
    }

    virtual String^ ToLine() override
    {
        return String::Join(" ", gcnew array<String^>{ OrderId, Date, Time, State });
    }
};

// One line of Bill.txt
public ref class BillRecord : public Record
{
public:
    String^ Id;                 // 9 digits, unique
    String^ CustomerName;
    String^ OrderId;
    double TotalWithoutTax;
    double TotalTax;
    double TotalDiscount;       // sum of the discounts on all bought products
    double Payable;
    double AmountReceived;
    double Balance;             // balance returned to the customer
    String^ Status;             // order status at the time of the bill
    double SeasonalDiscount;
    double SpecialDiscount;
    String^ Date;
    String^ Time;

    // nullptr for lines of an older Bill.txt layout (they are skipped, never rewritten)
    static BillRecord^ TryFromLine(String^ line)
    {
        array<String^>^ f = line->Split(gcnew array<Char>{ L' ' }, StringSplitOptions::RemoveEmptyEntries);
        if (f->Length != 14)
            return nullptr;

        try
        {
            BillRecord^ r = gcnew BillRecord();
            r->Id = f[0];
            r->CustomerName = Fmt::Unpack(f[1]);
            r->OrderId = f[2];
            r->TotalWithoutTax = Fmt::Parse(f[3]);
            r->TotalTax = Fmt::Parse(f[4]);
            r->TotalDiscount = Fmt::Parse(f[5]);
            r->Payable = Fmt::Parse(f[6]);
            r->AmountReceived = Fmt::Parse(f[7]);
            r->Balance = Fmt::Parse(f[8]);
            r->Status = f[9];
            r->SeasonalDiscount = Fmt::Parse(f[10]);
            r->SpecialDiscount = Fmt::Parse(f[11]);
            r->Date = f[12];
            r->Time = f[13];
            return r;
        }
        catch (Exception^)
        {
            return nullptr;
        }
    }

    virtual String^ ToLine() override
    {
        return String::Join(" ", gcnew array<String^>
        {
            Id, Fmt::Pack(CustomerName), OrderId, Fmt::Money(TotalWithoutTax), Fmt::Money(TotalTax),
                Fmt::Money(TotalDiscount), Fmt::Money(Payable), Fmt::Money(AmountReceived),
                Fmt::Money(Balance), Status, Fmt::Money(SeasonalDiscount), Fmt::Money(SpecialDiscount),
                Date, Time
        });
    }
};

// One line of BillDetail.txt: one bought product of a bill
public ref class BillDetailRecord : public Record
{
public:
    String^ BillId;
    String^ Acronym;
    double UnitPrice;           // without tax
    double Quantity;
    double Discount;            // total on this product (10 bought x 5 rupees = 50)

    BillDetailRecord(String^ billId, String^ acronym, double unitPrice, double quantity, double discount)
    {
        BillId = billId;
        Acronym = acronym;
        UnitPrice = unitPrice;
        Quantity = quantity;
        Discount = discount;
    }

    static BillDetailRecord^ TryFromLine(String^ line)
    {
        array<String^>^ f = line->Split(gcnew array<Char>{ L' ' }, StringSplitOptions::RemoveEmptyEntries);
        if (f->Length != 5)
            return nullptr;

        try
        {
            return gcnew BillDetailRecord(f[0], f[1], Fmt::Parse(f[2]), Fmt::Parse(f[3]), Fmt::Parse(f[4]));
        }
        catch (Exception^)
        {
            return nullptr;
        }
    }

    virtual String^ ToLine() override
    {
        return String::Join(" ", gcnew array<String^>
        {
            BillId, Acronym, Fmt::Money(UnitPrice), Fmt::Number(Quantity), Fmt::Money(Discount)
        });
    }
};

// A bill: the paid order, its products and the money part
public ref class Bill
{
public:
    String^ Id;                 // 9 digits
    Order^ Source;
    Cart^ Lines;
    double SeasonalDiscount;
    double SpecialDiscount;
    double AmountReceived;
    double Balance;
    DateTime When;

    // Final amount: cart payable - seasonal - special discount
    double Payable()
    {
        return Math::Round(Lines->Payable() - SeasonalDiscount - SpecialDiscount, 2);
    }

    // The Bill.txt line
    BillRecord^ ToRecord()
    {
        BillRecord^ r = gcnew BillRecord();
        r->Id = Id;
        r->CustomerName = Source->CustomerName;
        r->OrderId = Source->Id;
        r->TotalWithoutTax = Lines->TotalWithoutTax();
        r->TotalTax = Lines->TotalTax();
        r->TotalDiscount = Lines->TotalDiscount();
        r->Payable = Payable();
        r->AmountReceived = AmountReceived;
        r->Balance = Balance;
        r->Status = Source->Status;
        r->SeasonalDiscount = SeasonalDiscount;
        r->SpecialDiscount = SpecialDiscount;
        r->Date = Fmt::DateText(When);
        r->Time = Fmt::TimeText(When);
        return r;
    }

    // The BillDetail.txt lines (one per product)
    List<BillDetailRecord^>^ ToDetailRecords()
    {
        List<BillDetailRecord^>^ list = gcnew List<BillDetailRecord^>();
        for each(OrderItem ^ line in Lines->Items)
            list->Add(gcnew BillDetailRecord(Id, line->Item->Acronym, line->Item->Price,
                line->Quantity, Lines->LineDiscount(line)));
        return list;
    }
};

// ---------------------------------------------------------------------------
// A report in table form (the screens show it, the PDF exporter prints it)
// ---------------------------------------------------------------------------
public ref class ReportTable
{
public:
    String^ Title;
    array<String^>^ Headers;
    List<array<String^>^>^ Rows;
    String^ Footer;             // totals line

    ReportTable(String^ title, array<String^>^ headers)
    {
        Title = title;
        Headers = headers;
        Rows = gcnew List<array<String^>^>();
        Footer = "";
    }

    void AddRow(array<String^>^ row)
    {
        Rows->Add(row);
    }
};

// ---------------------------------------------------------------------------
// Where orders, details, bills and approvals are kept (the workflow only knows
// these interfaces, so files can be replaced by a database without touching it)
// ---------------------------------------------------------------------------
public interface class IOrderRepository
{
    String^ NextId(DateTime when);                          // 8 digits, last two = day
    void Add(Order^ order);
    void Update(Order^ order);                              // replaces the order with the same id
    Order^ FindById(String^ id);                            // nullptr when unknown
    List<Order^>^ FindAll();
    List<Order^>^ FindByStatus(String^ status);
    int ChangeStatus(String^ from, String^ to);             // returns how many orders moved
};

public interface class IOrderDetailRepository
{
    List<OrderDetail^>^ FindByOrderId(String^ orderId);
    List<OrderDetail^>^ FindAll();
    void ReplaceFor(String^ orderId, List<OrderDetail^>^ details);
};

// Writing bills (Bill.txt and BillDetail.txt)
public interface class IBillWriter
{
    String^ NextId();                                       // 9 digits, never repeats
    void Add(Bill^ bill);
};

// Reading bills back (reports, printing, the 7 day rule)
public interface class IBillReader
{
    List<BillRecord^>^ FindAll();
    BillRecord^ FindLatestByOrder(String^ orderId);         // nullptr when the order has no bill
    List<BillDetailRecord^>^ FindDetails(String^ billId);
};



public interface class IApprovalRepository
{
    String^ StateOf(String^ orderId);                       // an ApprovalState value
    void Request(String^ orderId, DateTime when);           // pending request (replaces an old one)
    void SetState(String^ orderId, String^ state);
    List<EditApproval^>^ FindByState(String^ state);
};
