#pragma once

// =============================================================================
// Persistence.h - the .txt files:
//   Product.txt, PCategory.txt, Seasons.txt, Order.txt, OrderDetail.txt,
//   Bill.txt, BillDetail.txt, Approval.txt
//   Responsibility : reading and writing the data files, nothing else
//   Depends on     : Common.h, Domain.h (implements its repository interfaces)
//   Extension      : a database = new classes implementing the same interfaces
// =============================================================================

#include "Common.h"
#include "Domain.h"

// ---------------------------------------------------------------------------
// Product.txt
// ---------------------------------------------------------------------------
public ref class ProductStore : public IProductLookup, public IStockWriter, public IProductList, public IProductAdder
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
    // (skipping it would lose it the next time the products are saved).
    void Load()
    {
        products = gcnew List<Product^>();
        for each(String ^ line in RecordFile::ReadLines(path))
            products->Add(Product::FromLine(line, units));
        warning = FindDuplicate();
    }

    // Writes all products back (this also moves an old Product.txt to the new layout)
    virtual void Save()
    {
        List<String^>^ lines = gcnew List<String^>();
        for each(Product ^ p in products)
            lines->Add(p->ToLine());
        RecordFile::WriteLines(path, lines);
    }

    virtual List<Product^>^ All()
    {
        return products;
    }

    virtual void Add(Product^ product)
    {
        products->Add(product);
        Save();
    }

    // Highest product id + 1
    virtual int NextId()
    {
        int highest = 0;
        for each(Product ^ p in products)
        {
            if (p->Id > highest)
                highest = p->Id;
        }
        return highest + 1;
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

    // For the "Find product" window: an empty text gives every product
    virtual List<Product^>^ Search(String^ text)
    {
        List<Product^>^ found = gcnew List<Product^>();
        String^ lower = text->Trim()->ToLower();
        for each(Product ^ p in products)
        {
            if (lower->Length == 0 || p->Barcode->Contains(lower) ||
                p->Acronym->ToLower()->Contains(lower) || p->Name->ToLower()->Contains(lower))
                found->Add(p);
        }
        return found;
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
// PCategory.txt
// ---------------------------------------------------------------------------
public ref class FileCategoryRepository : public ICategoryRepository
{
private:
    String^ path;

    void SaveAll(List<ProductCategory^>^ all)
    {
        List<String^>^ lines = gcnew List<String^>();
        for each(ProductCategory ^ category in all)
            lines->Add(category->ToLine());
        RecordFile::WriteLines(path, lines);
    }

public:
    FileCategoryRepository(String^ path)
    {
        this->path = path;
    }

    virtual List<ProductCategory^>^ FindAll()
    {
        List<ProductCategory^>^ all = gcnew List<ProductCategory^>();
        for each(String ^ line in RecordFile::ReadLines(path))
            all->Add(ProductCategory::FromLine(line));
        return all;
    }

    virtual ProductCategory^ FindById(String^ id)
    {
        for each(ProductCategory ^ category in FindAll())
        {
            if (category->Id == id)
                return category;
        }
        return nullptr;
    }

    virtual void Add(ProductCategory^ category)
    {
        RecordFile::Append(path, category);
    }

    virtual void Update(ProductCategory^ category)
    {
        List<ProductCategory^>^ all = FindAll();
        for (int i = 0; i < all->Count; i++)
        {
            if (all[i]->Id == category->Id)
                all[i] = category;
        }
        SaveAll(all);
    }
};

// ---------------------------------------------------------------------------
// Seasons.txt
// ---------------------------------------------------------------------------
public ref class FileSeasonRepository : public ISeasonRepository
{
private:
    String^ path;

public:
    FileSeasonRepository(String^ path)
    {
        this->path = path;
    }

    virtual List<Season^>^ FindAll()
    {
        List<Season^>^ all = gcnew List<Season^>();
        for each(String ^ line in RecordFile::ReadLines(path))
            all->Add(Season::FromLine(line));
        return all;
    }

    virtual void Add(Season^ season)
    {
        RecordFile::Append(path, season);
    }

    virtual void SetActive(int index, bool active)
    {
        List<Season^>^ all = FindAll();
        if (index < 0 || index >= all->Count)
            throw gcnew ArgumentException("Select a season first.");

        all[index]->Active = active;

        List<String^>^ lines = gcnew List<String^>();
        for each(Season ^ season in all)
            lines->Add(season->ToLine());
        RecordFile::WriteLines(path, lines);
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

    virtual Order^ FindById(String^ id)
    {
        for each(Order ^ order in LoadAll())
        {
            if (order->Id == id)
                return order;
        }
        return nullptr;
    }

    virtual List<Order^>^ FindAll()
    {
        return LoadAll();
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

    virtual List<OrderDetail^>^ FindAll()
    {
        List<OrderDetail^>^ all = gcnew List<OrderDetail^>();
        for each(String ^ line in RecordFile::ReadLines(path))
            all->Add(OrderDetail::FromLine(line));
        return all;
    }

    virtual List<OrderDetail^>^ FindByOrderId(String^ orderId)
    {
        List<OrderDetail^>^ found = gcnew List<OrderDetail^>();
        for each(OrderDetail ^ detail in FindAll())
        {
            if (detail->OrderId == orderId)
                found->Add(detail);
        }
        return found;
    }

    // Stores the details of one order (the old ones of the same order are dropped)
    virtual void ReplaceFor(String^ orderId, List<OrderDetail^>^ details)
    {
        List<String^>^ lines = gcnew List<String^>();
        for each(OrderDetail ^ detail in FindAll())
        {
            if (detail->OrderId != orderId)
                lines->Add(detail->ToLine());
        }
        for each(OrderDetail ^ detail in details)
            lines->Add(detail->ToLine());
        RecordFile::WriteLines(path, lines);
    }
};

// ---------------------------------------------------------------------------
// Bill.txt and BillDetail.txt (append only: a bill is never changed afterwards)
// ---------------------------------------------------------------------------
public ref class FileBillRepository : public IBillWriter, public IBillReader
{
private:
    literal int IdDigits = 9;

    String^ billPath;
    String^ detailPath;

public:
    FileBillRepository(String^ billPath, String^ detailPath)
    {
        this->billPath = billPath;
        this->detailPath = detailPath;
    }

    // 9 digit running number: the highest bill id in the file + 1, so it never repeats
    virtual String^ NextId()
    {
        int next = RecordFile::NextNumber(billPath, IdDigits);
        return next.ToString()->PadLeft(IdDigits, L'0');
    }

    virtual void Add(Bill^ bill)
    {
        RecordFile::Append(billPath, bill->ToRecord());
        for each(BillDetailRecord ^ detail in bill->ToDetailRecords())
            RecordFile::Append(detailPath, detail);
    }

    // Lines of an older Bill.txt layout are skipped
    virtual List<BillRecord^>^ FindAll()
    {
        List<BillRecord^>^ all = gcnew List<BillRecord^>();
        for each(String ^ line in RecordFile::ReadLines(billPath))
        {
            BillRecord^ record = BillRecord::TryFromLine(line);
            if (record != nullptr)
                all->Add(record);
        }
        return all;
    }

    // The last bill written for an order (an edited order has more than one)
    virtual BillRecord^ FindLatestByOrder(String^ orderId)
    {
        BillRecord^ latest = nullptr;
        for each(BillRecord ^ record in FindAll())
        {
            if (record->OrderId == orderId)
                latest = record;
        }
        return latest;
    }

    virtual List<BillDetailRecord^>^ FindDetails(String^ billId)
    {
        List<BillDetailRecord^>^ found = gcnew List<BillDetailRecord^>();
        for each(String ^ line in RecordFile::ReadLines(detailPath))
        {
            BillDetailRecord^ detail = BillDetailRecord::TryFromLine(line);
            if (detail != nullptr && detail->BillId == billId)
                found->Add(detail);
        }
        return found;
    }
};

// ---------------------------------------------------------------------------
// Approval.txt: requests to edit a paid order and the manager's answer
// ---------------------------------------------------------------------------
public ref class FileApprovalRepository : public IApprovalRepository
{
private:
    String^ path;

    List<EditApproval^>^ LoadAll()
    {
        List<EditApproval^>^ all = gcnew List<EditApproval^>();
        for each(String ^ line in RecordFile::ReadLines(path))
            all->Add(EditApproval::FromLine(line));
        return all;
    }

    void SaveAll(List<EditApproval^>^ all)
    {
        List<String^>^ lines = gcnew List<String^>();
        for each(EditApproval ^ approval in all)
            lines->Add(approval->ToLine());
        RecordFile::WriteLines(path, lines);
    }

public:
    FileApprovalRepository(String^ path)
    {
        this->path = path;
    }

    virtual String^ StateOf(String^ orderId)
    {
        for each(EditApproval ^ approval in LoadAll())
        {
            if (approval->OrderId == orderId)
                return approval->State;
        }
        return ApprovalState::None;
    }

    // One record per order: a new request replaces the old answer
    virtual void Request(String^ orderId, DateTime when)
    {
        List<EditApproval^>^ all = LoadAll();
        EditApproval^ request = gcnew EditApproval(orderId, Fmt::DateText(when), Fmt::TimeText(when), ApprovalState::Pending);

        bool replaced = false;
        for (int i = 0; i < all->Count; i++)
        {
            if (all[i]->OrderId == orderId)
            {
                all[i] = request;
                replaced = true;
            }
        }
        if (!replaced)
            all->Add(request);
        SaveAll(all);
    }

    virtual void SetState(String^ orderId, String^ state)
    {
        List<EditApproval^>^ all = LoadAll();
        for each(EditApproval ^ approval in all)
        {
            if (approval->OrderId == orderId)
                approval->State = state;
        }
        SaveAll(all);
    }

    virtual List<EditApproval^>^ FindByState(String^ state)
    {
        List<EditApproval^>^ found = gcnew List<EditApproval^>();
        for each(EditApproval ^ approval in LoadAll())
        {
            if (approval->State == state)
                found->Add(approval);
        }
        return found;
    }
};
