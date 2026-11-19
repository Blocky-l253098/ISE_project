#pragma once

// =============================================================================
// Domain.h - the business model: products, carts, orders, bills and the rules
//            that may change (stock rule, pricing, payment methods)
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
    // Position of the unit field: it is the first allowed unit word (after the
    // name) that is followed by four numbers. This lets the name AND the barcode
    // file path contain spaces.
    static int FindUnit(array<String^>^ f, NameSet^ units)
    {
        for (int i = 4; i <= f->Length - 6; i++)
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
    String^ Acronym;
    String^ Name;
    String^ Unit;           // one of the allowed units (count / weight)
    double Price;           // per unit, without tax
    double Tax;             // per unit, PKR
    double Discount;        // per unit, PKR
    double Quantity;        // available in stock
    String^ BarcodeFile;

    // "id barcode acronym name... unit price tax discount quantity file..."
    static Product^ FromLine(String^ line, NameSet^ units)
    {
        array<String^>^ f = line->Split(gcnew array<Char>{ L' ' }, StringSplitOptions::RemoveEmptyEntries);
        if (f->Length < 10)
            throw gcnew FormatException("Bad product line: " + line);

        int u = FindUnit(f, units);
        if (u < 0)
            throw gcnew FormatException("Bad product line (unit must be " +
                String::Join(" or ", units->ToArray()) + ", then price, tax, discount, " +
                "quantity and barcode file): " + line);

        Product^ p = gcnew Product();
        p->Id = Int32::Parse(f[0]);
        p->Barcode = f[1];
        p->Acronym = f[2];
        p->Name = String::Join(" ", f, 3, u - 3);
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
            Id.ToString(), Barcode, Acronym, Name, Unit, Fmt::Number(Price),
                Fmt::Number(Tax), Fmt::Number(Discount), Fmt::Number(Quantity), BarcodeFile
        });
    }

    // Checks the details typed on the barcode screen before they are written to
    // Product.txt. Throws an ArgumentException that says what to fix.
    void Validate(NameSet^ units)
    {
        if (!Regex::IsMatch(Acronym, "^[^\\s#]{1,8}$"))
            throw gcnew ArgumentException("Acronym must be 1 to 8 characters, without spaces or '#'.");

        if (String::IsNullOrWhiteSpace(Name) || Name->Contains("#"))
            throw gcnew ArgumentException("Enter a product name (the '#' character is not allowed).");

        if (!units->Contains(Unit))
            throw gcnew ArgumentException("Unit must be " + String::Join(" or ", units->ToArray()) + ".");

        if (Price < 0 || Tax < 0 || Discount < 0 || Quantity < 0)
            throw gcnew ArgumentException("Price, tax, discount and quantity cannot be negative.");

        if (String::IsNullOrWhiteSpace(BarcodeFile) || BarcodeFile->Contains("#"))
            throw gcnew ArgumentException("The barcode file path is empty or contains '#'.");

        // The line must be read back as the same product (the name and the barcode
        // file may contain spaces, so a name like "Tea weight 1 2 3 4" would be ambiguous)
        Product^ back = FromLine(ToLine(), units);
        if (back->Name != Name || back->Unit != Unit || back->BarcodeFile != BarcodeFile)
            throw gcnew ArgumentException("This name cannot be stored in Product.txt unambiguously. Change the name.");
    }
};

// Reading products (small interface: the screens and the workflow only look products up)
public interface class IProductLookup
{
    Product^ FindByBarcode(String^ barcode);     // nullptr when unknown
    Product^ FindById(int id);                   // nullptr when unknown
    String^ Summary();                           // short text about what was loaded
};

// Adding / changing products (separate interface: only the barcode screen needs it)
public interface class IProductRegistry
{
    Product^ FindByBarcode(String^ barcode);     // nullptr when unknown
    array<String^>^ UnitNames();                 // the allowed units, for the product form
    void Check(Product^ product);                // throws when the details cannot be stored
    bool Register(Product^ product);             // writes Product.txt; true = new, false = updated
};

// Writing the stock back (separate interface: only the checkout needs it)
public interface class IStockWriter
{
    void Save();
};

// ---------------------------------------------------------------------------
// Rules that may change
// ---------------------------------------------------------------------------

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

// The products of the order being recorded + its totals
public ref class Cart
{
private:
    IStockRule^ rule;
    IPricingPolicy^ pricing;
    List<OrderItem^>^ items;

    OrderItem^ Find(Product^ product)
    {
        for each(OrderItem ^ line in items)
        {
            if (line->Item == product)
                return line;
        }
        return nullptr;
    }

public:
    Cart(IStockRule^ rule, IPricingPolicy^ pricing)
    {
        this->rule = rule;
        this->pricing = pricing;
        items = gcnew List<OrderItem^>();
    }

    property List<OrderItem^>^ Items
    {
        List<OrderItem^> ^ get() { return items; }
    }

    // false when the stock rule refuses the quantity (nothing is added then)
    bool Add(Product^ product, double quantity)
    {
        OrderItem^ line = Find(product);
        double wanted = quantity + ((line == nullptr) ? 0 : line->Quantity);

        if (!rule->Allows(wanted, product->Quantity))
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
        if (quantity <= 0 || !rule->Allows(quantity, line->Item->Quantity))
            return false;
        line->Quantity = quantity;
        return true;
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

    double TotalDiscount()
    {
        double total = 0;
        for each(OrderItem ^ line in items) total += LineDiscount(line);
        return total;
    }

    double Payable()
    {
        return TotalWithoutTax() + TotalTax() - TotalDiscount();
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

    OrderDraft(String^ customerName, String^ customerContact, String^ paymentMode, String^ merchant)
    {
        CustomerName = customerName;
        CustomerContact = customerContact;
        PaymentMode = paymentMode;
        Merchant = merchant;
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

// A bill: the paid order and its products
public ref class Bill
{
public:
    String^ Id;             // 9 digits
    Order^ Source;
    Cart^ Lines;
};

// ---------------------------------------------------------------------------
// Where orders, details and bills are kept (the workflow only knows these
// interfaces, so files can be replaced by a database without touching it)
// ---------------------------------------------------------------------------
public interface class IOrderRepository
{
    String^ NextId(DateTime when);                          // 8 digits, last two = day
    void Add(Order^ order);
    void Update(Order^ order);                              // replaces the order with the same id
    List<Order^>^ FindByStatus(String^ status);
    int ChangeStatus(String^ from, String^ to);             // returns how many orders moved
};

public interface class IOrderDetailRepository
{
    List<OrderDetail^>^ FindByOrderId(String^ orderId);
    void ReplaceFor(String^ orderId, List<OrderDetail^>^ details);
};

public interface class IBillRepository
{
    String^ NextId();                                       // 9 digits
    void Add(Bill^ bill);
};
