#pragma once

// =============================================================================
// Persistence.h - the .txt files: Product.txt, Order.txt, OrderDetail.txt, Bill.txt
//   Responsibility : reading and writing the data files, nothing else
//   Depends on     : Common.h, Domain.h (implements its repository interfaces)
//   Extension      : a database = new classes implementing the same interfaces;
//                    a different Bill.txt layout = a new IBillFormat
// =============================================================================

#include "Common.h"
#include "Domain.h"

// ---------------------------------------------------------------------------
// Product.txt
// ---------------------------------------------------------------------------
public ref class ProductStore : public IProductLookup, public IStockWriter, public IProductRegistry
{
private:
    String^ path;
    NameSet^ units;
    List<Product^>^ products;
    String^ warning;

    // Text about the first duplicate barcode / id, nullptr when there is none
    String^ FindDuplicate()
    {
        for (int i = 0; i < products->Count; i++)
        {
            for (int j = i + 1; j < products->Count; j++)
            {
                if (products[i]->Barcode == products[j]->Barcode)
                    return "duplicate barcode " + products[i]->Barcode;
                if (products[i]->Id == products[j]->Id)
                    return "duplicate product id " + products[i]->Id.ToString();
            }
        }
        return nullptr;
    }

public:
    ProductStore(String^ path, NameSet^ units)
    {
        this->path = path;
        this->units = units;
        Load();
    }

    // Reads Product.txt. A bad line stops the loading with a message that names it
    // (skipping it would lose it the next time the stock is saved).
    void Load()
    {
        products = gcnew List<Product^>();
        for each(String ^ line in RecordFile::ReadLines(path))
            products->Add(Product::FromLine(line, units));
        warning = FindDuplicate();
    }

    // Writes the current stock back to Product.txt
    virtual void Save()
    {
        List<String^>^ lines = gcnew List<String^>();
        for each(Product ^ p in products)
            lines->Add(p->ToLine());
        RecordFile::WriteLines(path, lines);
    }

    // ---- IProductRegistry: products added from the barcode screen ----

    virtual array<String^>^ UnitNames()
    {
        return units->ToArray();
    }

    // Throws when the details are invalid or the acronym already belongs to another product
    virtual void Check(Product^ product)
    {
        product->Validate(units);

        for each(Product ^ other in products)
        {
            if (other->Barcode != product->Barcode && other->Acronym == product->Acronym)
                throw gcnew ArgumentException("Acronym " + product->Acronym +
                    " is already used by product " + other->Barcode + ".");
        }
    }

    // New barcode -> a new line at the end of Product.txt (next free product id).
    // Known barcode -> its line is updated (the product id stays the same).
    virtual bool Register(Product^ product)
    {
        Load();                                 // work on what Product.txt holds right now
        Check(product);

        Product^ existing = FindByBarcode(product->Barcode);
        bool isNew = (existing == nullptr);

        if (isNew)
        {
            int highest = 0;
            for each(Product ^ p in products)
            {
                if (p->Id > highest)
                    highest = p->Id;
            }
            product->Id = highest + 1;
            products->Add(product);
        }
        else
        {
            existing->Acronym = product->Acronym;
            existing->Name = product->Name;
            existing->Unit = product->Unit;
            existing->Price = product->Price;
            existing->Tax = product->Tax;
            existing->Discount = product->Discount;
            existing->Quantity = product->Quantity;
            existing->BarcodeFile = product->BarcodeFile;
        }

        Save();
        return isNew;
    }

    virtual Product^ FindByBarcode(String^ barcode)
    {
        if (barcode == nullptr)
            return nullptr;

        for each(Product ^ p in products)
        {
            if (p->Barcode->Trim() == barcode->Trim())
                return p;
        }
        return nullptr;
    }

    virtual Product^ FindById(int id)
    {
        for each(Product ^ p in products)
        {
            if (p->Id == id)
                return p;
        }
        return nullptr;
    }

    // Where the products came from (shown when something does not match)
    virtual String^ Summary()
    {
        if (!File::Exists(path))
            return "Product.txt not found. Looked for: " + path;

        String^ list = "";
        int shown = 0;
        for each(Product ^ p in products)
        {
            if (shown >= 5)
            {
                list += ", ...";
                break;
            }
            if (shown > 0)
                list += ", ";
            list += p->Barcode;
            shown++;
        }

        String^ text = Convert::ToString(products->Count) + " products in " + path +
            " (barcodes: " + list + ")";
        if (warning != nullptr)
            text += "  WARNING: " + warning;
        return text;
    }
};

// ---------------------------------------------------------------------------
// Order.txt
// ---------------------------------------------------------------------------
public ref class FileOrderRepository : public IOrderRepository
{
private:
    literal int SequenceDigits = 6;

    String^ path;

    List<Order^>^ LoadAll()
    {
        List<Order^>^ all = gcnew List<Order^>();
        for each(String ^ line in RecordFile::ReadLines(path))
            all->Add(Order::FromLine(line));
        return all;
    }

    void SaveAll(List<Order^>^ all)
    {
        List<String^>^ lines = gcnew List<String^>();
        for each(Order ^ order in all)
            lines->Add(order->ToLine());
        RecordFile::WriteLines(path, lines);
    }

public:
    FileOrderRepository(String^ path)
    {
        this->path = path;
    }

    // 6 digit running number + 2 digit day of the order = 8 digits
    virtual String^ NextId(DateTime when)
    {
        int next = RecordFile::NextNumber(path, SequenceDigits);
        return next.ToString()->PadLeft(SequenceDigits, L'0') + when.ToString("dd", CultureInfo::InvariantCulture);
    }

    virtual void Add(Order^ order)
    {
        RecordFile::Append(path, order);
    }

    // Replaces the stored order that has the same id
    virtual void Update(Order^ order)
    {
        List<Order^>^ all = LoadAll();
        for (int i = 0; i < all->Count; i++)
        {
            if (all[i]->Id == order->Id)
                all[i] = order;
        }
        SaveAll(all);
    }

    virtual List<Order^>^ FindByStatus(String^ status)
    {
        List<Order^>^ found = gcnew List<Order^>();
        for each(Order ^ order in LoadAll())
        {
            if (order->Status == status)
                found->Add(order);
        }
        return found;
    }

    // Moves every order from one status to another, returns how many
    virtual int ChangeStatus(String^ from, String^ to)
    {
        List<Order^>^ all = LoadAll();
        int changed = 0;
        for each(Order ^ order in all)
        {
            if (order->Status == from)
            {
                order->Status = to;
                changed++;
            }
        }
        if (changed > 0)
            SaveAll(all);
        return changed;
    }
};

// ---------------------------------------------------------------------------
// OrderDetail.txt
// ---------------------------------------------------------------------------
public ref class FileOrderDetailRepository : public IOrderDetailRepository
{
private:
    String^ path;

public:
    FileOrderDetailRepository(String^ path)
    {
        this->path = path;
    }

    virtual List<OrderDetail^>^ FindByOrderId(String^ orderId)
    {
        List<OrderDetail^>^ found = gcnew List<OrderDetail^>();
        for each(String ^ line in RecordFile::ReadLines(path))
        {
            OrderDetail^ detail = OrderDetail::FromLine(line);
            if (detail->OrderId == orderId)
                found->Add(detail);
        }
        return found;
    }

    // Stores the details of one order (the old ones of the same order are dropped)
    virtual void ReplaceFor(String^ orderId, List<OrderDetail^>^ details)
    {
        List<String^>^ lines = gcnew List<String^>();
        for each(String ^ line in RecordFile::ReadLines(path))
        {
            if (OrderDetail::FromLine(line)->OrderId != orderId)
                lines->Add(line);
        }
        for each(OrderDetail ^ detail in details)
            lines->Add(detail->ToLine());
        RecordFile::WriteLines(path, lines);
    }
};

// ---------------------------------------------------------------------------
// Bill.txt: the layout of the lines is a separate, replaceable strategy
// ---------------------------------------------------------------------------
public interface class IBillFormat
{
    List<String^>^ ToLines(Bill^ bill);
};

// One bill = one line; the products are listed with ',' between them
// (acronyms, prices, discounts and quantities in the same order)
public ref class OneLinePerBillFormat : public IBillFormat
{
public:
    virtual List<String^>^ ToLines(Bill^ bill)
    {
        List<String^>^ acronyms = gcnew List<String^>();
        List<String^>^ prices = gcnew List<String^>();
        List<String^>^ discounts = gcnew List<String^>();
        List<String^>^ quantities = gcnew List<String^>();

        for each(OrderItem ^ line in bill->Lines->Items)
        {
            acronyms->Add(line->Item->Acronym);
            prices->Add(Fmt::Number(line->Item->Price));
            discounts->Add(Fmt::Number(line->Item->Discount));
            quantities->Add(Fmt::Number(line->Quantity));
        }

        List<String^>^ lines = gcnew List<String^>();
        lines->Add(String::Join(" ", gcnew array<String^>
        {
            bill->Id, Fmt::Pack(bill->Source->CustomerName), bill->Source->Id,
                String::Join(",", acronyms->ToArray()),
                String::Join(",", prices->ToArray()),
                String::Join(",", discounts->ToArray()),
                String::Join(",", quantities->ToArray()),
                Fmt::Number(bill->Lines->TotalWithoutTax()),
                Fmt::Number(bill->Lines->TotalTax()),
                Fmt::Number(bill->Lines->TotalDiscount()),
                Fmt::Number(bill->Lines->Payable()),
                bill->Source->Status
        }));
        return lines;
    }
};

// Alternative: one line per product (same bill id on every line, totals repeated)
public ref class OneLinePerProductFormat : public IBillFormat
{
public:
    virtual List<String^>^ ToLines(Bill^ bill)
    {
        List<String^>^ lines = gcnew List<String^>();

        for each(OrderItem ^ line in bill->Lines->Items)
        {
            lines->Add(String::Join(" ", gcnew array<String^>
            {
                bill->Id, Fmt::Pack(bill->Source->CustomerName), bill->Source->Id,
                    line->Item->Acronym,
                    Fmt::Number(line->Item->Price),
                    Fmt::Number(line->Item->Discount),
                    Fmt::Number(line->Quantity),
                    Fmt::Number(bill->Lines->TotalWithoutTax()),
                    Fmt::Number(bill->Lines->TotalTax()),
                    Fmt::Number(bill->Lines->TotalDiscount()),
                    Fmt::Number(bill->Lines->Payable()),
                    bill->Source->Status
            }));
        }
        return lines;
    }
};

public ref class FileBillRepository : public IBillRepository
{
private:
    literal int IdDigits = 9;

    String^ path;
    IBillFormat^ format;

public:
    FileBillRepository(String^ path, IBillFormat^ format)
    {
        this->path = path;
        this->format = format;
    }

    // 9 digit running number
    virtual String^ NextId()
    {
        int next = RecordFile::NextNumber(path, IdDigits);
        return next.ToString()->PadLeft(IdDigits, L'0');
    }

    virtual void Add(Bill^ bill)
    {
        for each(String ^ line in format->ToLines(bill))
            RecordFile::AppendLine(path, line);
    }
};
