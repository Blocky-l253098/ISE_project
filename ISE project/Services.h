#pragma once

// =============================================================================
// Services.h - the order workflow (no files and no screens in here)
//   new -> [Hold] -> on-hold -> [Open] -> in-progress -> [Hold / Release] -> on-hold
//   new / in-progress -> [Pay] -> paid -> [ArchivePaid] -> archived
//   paid -> [Open: manager approval, 7 days] -> edited -> [Pay] -> paid
//   Responsibility : the rules of recording, holding, editing, paying and archiving
//   Depends on     : Common.h, Domain.h (interfaces only - no file classes)
// =============================================================================

#include "Common.h"
#include "Domain.h"

// The seasonal discount: the highest active season that covers the date
public ref class SeasonCalendar : public ISeasonalDiscount
{
private:
    ISeasonRepository^ seasons;

public:
    SeasonCalendar(ISeasonRepository^ seasons)
    {
        this->seasons = seasons;
    }

    virtual double PercentageOn(DateTime when)
    {
        double best = 0;
        for each(Season ^ season in seasons->FindAll())
        {
            if (season->Covers(when) && season->Percentage > best)
                best = season->Percentage;
        }
        return best;
    }
};

// The rules the workflow follows, grouped so the constructor stays short
public ref class OrderPolicies
{
public:
    IStockRule^ Stock;
    IPricingPolicy^ Pricing;
    PaymentCatalog^ Payments;
    ISeasonalDiscount^ Seasons;

    OrderPolicies(IStockRule^ stock, IPricingPolicy^ pricing, PaymentCatalog^ payments, ISeasonalDiscount^ seasons)
    {
        Stock = stock;
        Pricing = pricing;
        Payments = payments;
        Seasons = seasons;
    }
};

// The places the workflow reads and writes, grouped for the same reason
public ref class OrderStores
{
public:
    IOrderRepository^ Orders;
    IOrderDetailRepository^ Details;
    IBillWriter^ BillWriter;
    IBillReader^ BillReader;
    IApprovalRepository^ Approvals;

    OrderStores(IOrderRepository^ orders, IOrderDetailRepository^ details, IBillWriter^ billWriter,
        IBillReader^ billReader, IApprovalRepository^ approvals)
    {
        Orders = orders;
        Details = details;
        BillWriter = billWriter;
        BillReader = billReader;
        Approvals = approvals;
    }
};

// What an order screen needs from the workflow
public interface class IOrderWorkflow
{
    Cart^ NewCart();
    array<String^>^ PaymentModeNames();

    // The amounts the bill would have (seasonal and special discount included)
    PriceQuote^ Quote(Cart^ cart, double specialPercent);

    // Saves the order as on-hold (no bill, stock is not touched)
    Order^ Hold(Order^ current, OrderDraft^ draft, Cart^ cart);

    // Saves the order as paid, writes the bill and updates the stock
    Bill^ Pay(Order^ current, OrderDraft^ draft, Cart^ cart);

    List<Order^>^ HeldOrders();                 // on-hold orders, newest first
    List<Order^>^ EditableOrders();             // every order that is not archived, newest first

    // Opens an existing order: on-hold orders are resumed; paid orders need the
    // manager's approval and must be within 7 days of the payment
    Cart^ Open(Order^ order);

    // Puts an in-progress order back on hold (the screen was left without paying)
    void Release(Order^ order);
};

// What the manager's archive button needs
public interface class IOrderArchiver
{
    int ArchivePaid();
};

// What the manager's approval screen needs
public interface class IApprovalDesk
{
    List<Order^>^ Pending();                    // paid orders waiting for approval
    void Approve(String^ orderId);
    void Reject(String^ orderId);
};

public ref class OrderService : public IOrderWorkflow
{
private:
    literal int EditWindowDays = 7;

    IProductLookup^ products;
    IStockWriter^ stock;
    OrderStores^ stores;
    OrderPolicies^ policies;
    IClock^ clock;

    void ValidateCart(Cart^ cart)
    {
        if (cart->Items->Count == 0)
            throw gcnew InvalidOperationException("Scan at least one product first.");
    }

    // Checks the payment part of the draft and returns the chosen method
    PaymentMethod^ ValidatePayment(OrderDraft^ draft)
    {
        PaymentMethod^ method = policies->Payments->Find(draft->PaymentMode);
        if (method == nullptr)
            throw gcnew ArgumentException("Choose a payment mode.");

        if (method->NeedsMerchant && String::IsNullOrEmpty(draft->Merchant))
            throw gcnew ArgumentException("Enter the merchant name (e.g. ABL, JazzCash) for " +
                draft->PaymentMode + " payments.");

        if (draft->SpecialPercent < 0 || draft->SpecialPercent > DiscountRules::MaxSpecialPercent)
            throw gcnew ArgumentException("The special discount cannot be more than " +
                Fmt::Number(DiscountRules::MaxSpecialPercent) + " percent.");

        return method;
    }

    // A customer who gives no name is a walk-in customer (fewer keystrokes at the till)
    String^ NameOf(OrderDraft^ draft)
    {
        if (String::IsNullOrEmpty(draft->CustomerName))
            return "Walk-in";
        return draft->CustomerName;
    }

    PriceQuote^ MakeQuote(Cart^ cart, double specialPercent, DateTime when)
    {
        double baseAmount = cart->Payable();
        double seasonalPercent = policies->Seasons->PercentageOn(when);
        double seasonal = Math::Round(baseAmount * seasonalPercent / 100.0, 2);
        double special = Math::Round(baseAmount * specialPercent / 100.0, 2);
        return gcnew PriceQuote(baseAmount, seasonal, special, Math::Round(baseAmount - seasonal - special, 2));
    }

    List<OrderDetail^>^ DetailsOf(String^ orderId, Cart^ cart)
    {
        List<OrderDetail^>^ list = gcnew List<OrderDetail^>();
        for each(OrderItem ^ line in cart->Items)
            list->Add(gcnew OrderDetail(orderId, line->Item->Id, line->Item->Barcode, line->Quantity));
        return list;
    }

    // A new order (new id, date, time) or a COPY of the current one with the draft's data
    Order^ Prepare(Order^ current, OrderDraft^ draft, DateTime now)
    {
        Order^ order = (current == nullptr) ? gcnew Order() : current->Clone();
        if (current == nullptr)
        {
            order->Id = stores->Orders->NextId(now);
            order->Date = Fmt::DateText(now);
            order->Time = Fmt::TimeText(now);
        }

        order->CustomerName = NameOf(draft);
        order->CustomerContact = draft->CustomerContact;
        order->PaymentMode = draft->PaymentMode;
        order->Merchant = draft->Merchant;
        return order;
    }

    // Writes the order and its details
    void Persist(Order^ order, bool isNew, Cart^ cart)
    {
        if (isNew)
            stores->Orders->Add(order);
        else
            stores->Orders->Update(order);

        stores->Details->ReplaceFor(order->Id, DetailsOf(order->Id, cart));
    }

    // sign +1: take the cart out of the stock; -1: put it back.
    // For an edited paid order only the DIFFERENCE to the old quantities moves.
    void ApplyStock(Cart^ cart, int sign)
    {
        for each(Product ^ product in cart->AffectedProducts())
            product->Quantity -= sign * cart->StockChange(product);
    }

    // Undo of the stock change after a failed payment (best effort)
    void RestoreStock(Cart^ cart)
    {
        ApplyStock(cart, -1);
        try
        {
            stock->Save();
        }
        catch (Exception^)
        {
            // the original error is the one the cashier sees
        }
    }

    // A used approval cannot be used twice
    void MarkApprovalUsed(String^ orderId)
    {
        try
        {
            if (stores->Approvals->StateOf(orderId) == ApprovalState::Approved)
                stores->Approvals->SetState(orderId, ApprovalState::Used);
        }
        catch (Exception^)
        {
            // the bill is already written; nothing more to do
        }
    }

    // The cart of an existing order. 'takenFromStock' = a paid order: its quantities
    // are already out of the stock, so they count as available while it is edited.
    Cart^ BuildCart(Order^ order, bool takenFromStock)
    {
        Cart^ cart = NewCart();
        for each(OrderDetail ^ detail in stores->Details->FindByOrderId(order->Id))
        {
            Product^ product = products->FindById(detail->ProductId);
            if (product == nullptr)
                throw gcnew InvalidOperationException("Product " + detail->ProductId.ToString() +
                    " of this order is no longer in Product.txt.");

            if (takenFromStock)
                cart->AddBaseline(product, detail->Quantity);

            if (!cart->Add(product, detail->Quantity))
                throw gcnew InvalidOperationException("Not enough stock of " + product->Acronym +
                    " to open this order.");
        }
        return cart;
    }

    // When was the order paid? The date of its last bill (the order date for old data)
    DateTime PaymentTime(Order^ order)
    {
        BillRecord^ last = stores->BillReader->FindLatestByOrder(order->Id);
        if (last != nullptr)
            return Fmt::ParseDateTime(last->Date, last->Time);
        return Fmt::ParseDate(order->Date);
    }

    // On-hold / in-progress order: back on the screen, marked in-progress
    Cart^ Resume(Order^ order)
    {
        Cart^ cart = BuildCart(order, false);
        order->Status = OrderStatus::InProgress;
        stores->Orders->Update(order);
        return cart;
    }

    // Paid order: only within 7 days of the payment and only with the manager's approval
    Cart^ OpenPaid(Order^ order)
    {
        DateTime now = clock->Now();
        TimeSpan age = now - PaymentTime(order);
        if (age.TotalDays > EditWindowDays)
            throw gcnew InvalidOperationException("A paid order can only be edited within " +
                Convert::ToString(EditWindowDays) + " days of the payment.");

        String^ state = stores->Approvals->StateOf(order->Id);
        if (state == ApprovalState::Approved)
            return BuildCart(order, true);

        if (state == ApprovalState::Pending)
            throw gcnew InvalidOperationException("The manager has not approved the edit of order " +
                order->Id + " yet.");

        stores->Approvals->Request(order->Id, now);
        throw gcnew InvalidOperationException("Editing a paid order needs the manager's approval. The request for order " +
            order->Id + " was sent: open the order again after the manager approves it.");
    }

public:
    OrderService(IProductLookup^ products, IStockWriter^ stock, OrderStores^ stores,
        OrderPolicies^ policies, IClock^ clock)
    {
        this->products = products;
        this->stock = stock;
        this->stores = stores;
        this->policies = policies;
        this->clock = clock;
    }

    // An empty cart that follows the stock rule and the pricing policy
    virtual Cart^ NewCart()
    {
        return gcnew Cart(policies->Stock, policies->Pricing);
    }

    virtual array<String^>^ PaymentModeNames()
    {
        return policies->Payments->Names();
    }

    virtual PriceQuote^ Quote(Cart^ cart, double specialPercent)
    {
        return MakeQuote(cart, specialPercent, clock->Now());
    }

    virtual Order^ Hold(Order^ current, OrderDraft^ draft, Cart^ cart)
    {
        ValidateCart(cart);
        if (current != nullptr && current->Status == OrderStatus::Paid)
            throw gcnew InvalidOperationException("A paid order cannot be put on hold. Generate the bill again or close the screen.");

        Order^ order = Prepare(current, draft, clock->Now());
        order->Status = OrderStatus::OnHold;
        order->Payment = cart->Payable();
        Persist(order, current == nullptr, cart);
        return order;
    }

    virtual Bill^ Pay(Order^ current, OrderDraft^ draft, Cart^ cart)
    {
        ValidateCart(cart);
        PaymentMethod^ method = ValidatePayment(draft);

        DateTime now = clock->Now();
        PriceQuote^ quote = MakeQuote(cart, draft->SpecialPercent, now);

        // cash can give change; card / wallet pay exactly the amount
        double received = quote->Payable;
        if (!method->NeedsMerchant && draft->AmountReceived >= 0)
            received = draft->AmountReceived;

        if (received < quote->Payable)
            throw gcnew ArgumentException("The amount received (" + Fmt::Money(received) +
                ") is less than the payable amount (" + Fmt::Money(quote->Payable) + ").");

        Order^ order = Prepare(current, draft, now);
        order->Status = OrderStatus::Paid;
        order->Payment = quote->Payable;

        Bill^ bill = gcnew Bill();
        bill->Id = stores->BillWriter->NextId();
        bill->Source = order;
        bill->Lines = cart;
        bill->SeasonalDiscount = quote->Seasonal;
        bill->SpecialDiscount = quote->Special;
        bill->AmountReceived = received;
        bill->Balance = Math::Round(received - quote->Payable, 2);
        bill->When = now;

        ApplyStock(cart, 1);
        try
        {
            stock->Save();
            stores->BillWriter->Add(bill);
            Persist(order, current == nullptr, cart);
        }
        catch (Exception^)
        {
            RestoreStock(cart);
            throw;
        }

        MarkApprovalUsed(order->Id);
        return bill;
    }

    virtual List<Order^>^ HeldOrders()
    {
        List<Order^>^ held = stores->Orders->FindByStatus(OrderStatus::OnHold);
        held->Reverse();
        return held;
    }

    virtual List<Order^>^ EditableOrders()
    {
        List<Order^>^ found = gcnew List<Order^>();
        for each(Order ^ order in stores->Orders->FindAll())
        {
            if (order->Status != OrderStatus::Archived)
                found->Add(order);
        }
        found->Reverse();
        return found;
    }

    virtual Cart^ Open(Order^ order)
    {
        if (order->Status == OrderStatus::Archived)
            throw gcnew InvalidOperationException("Archived orders cannot be edited.");

        if (order->Status == OrderStatus::Paid)
            return OpenPaid(order);

        return Resume(order);
    }

    virtual void Release(Order^ order)
    {
        order->Status = OrderStatus::OnHold;
        stores->Orders->Update(order);
    }
};

// Manager action: paid orders -> archived (separate from the workflow: it needs
// the order repository only, not the products or the cart)
public ref class OrderArchiver : public IOrderArchiver
{
private:
    IOrderRepository^ orders;

public:
    OrderArchiver(IOrderRepository^ orders)
    {
        this->orders = orders;
    }

    // Returns how many orders were archived
    virtual int ArchivePaid()
    {
        return orders->ChangeStatus(OrderStatus::Paid, OrderStatus::Archived);
    }
};

// Manager action: approve or reject the edit of a paid order
public ref class ApprovalService : public IApprovalDesk
{
private:
    IApprovalRepository^ approvals;
    IOrderRepository^ orders;

public:
    ApprovalService(IApprovalRepository^ approvals, IOrderRepository^ orders)
    {
        this->approvals = approvals;
        this->orders = orders;
    }

    virtual List<Order^>^ Pending()
    {
        List<Order^>^ found = gcnew List<Order^>();
        for each(EditApproval ^ request in approvals->FindByState(ApprovalState::Pending))
        {
            Order^ order = orders->FindById(request->OrderId);
            if (order != nullptr)
                found->Add(order);
        }
        return found;
    }

    virtual void Approve(String^ orderId)
    {
        approvals->SetState(orderId, ApprovalState::Approved);
    }

    virtual void Reject(String^ orderId)
    {
        approvals->SetState(orderId, ApprovalState::Rejected);
    }
};

// Who may use the manager features
public interface class IAuthenticator
{
    bool Login(String^ userName, String^ password);
};

// For now one fixed manager account; a user file or a database can replace it later
// (a new class implementing IAuthenticator, one line in PosFactory)
public ref class FixedCredentialAuthenticator : public IAuthenticator
{
private:
    String^ userName;
    String^ password;

public:
    FixedCredentialAuthenticator(String^ userName, String^ password)
    {
        this->userName = userName;
        this->password = password;
    }

    virtual bool Login(String^ userName, String^ password)
    {
        return userName != nullptr && password != nullptr &&
            userName->Trim() == this->userName && password == this->password;
    }
};
