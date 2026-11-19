#pragma once

// =============================================================================
// Services.h - the order workflow (no files and no screens in here)
//   new -> [Hold] -> on-hold -> [Resume] -> in-progress -> [Hold / Release] -> on-hold
//   new / in-progress -> [Pay] -> paid -> [ArchivePaid] -> archived
//   Responsibility : the rules of recording, holding, paying and archiving orders
//   Depends on     : Common.h, Domain.h (interfaces only - no file classes)
// =============================================================================

#include "Common.h"
#include "Domain.h"

// The rules the workflow follows, grouped so the constructor stays short
public ref class OrderPolicies
{
public:
    IStockRule^ Stock;
    IPricingPolicy^ Pricing;
    PaymentCatalog^ Payments;

    OrderPolicies(IStockRule^ stock, IPricingPolicy^ pricing, PaymentCatalog^ payments)
    {
        Stock = stock;
        Pricing = pricing;
        Payments = payments;
    }
};

// What an order screen needs from the workflow
public interface class IOrderWorkflow
{
    Cart^ NewCart();
    array<String^>^ PaymentModeNames();

    // Saves the order as on-hold (no bill, stock is not touched)
    Order^ Hold(Order^ current, OrderDraft^ draft, Cart^ cart);

    // Saves the order as paid, writes the bill and takes the products out of stock
    Bill^ Pay(Order^ current, OrderDraft^ draft, Cart^ cart);

    List<Order^>^ HeldOrders();

    // Loads a held order back into a cart and marks it in-progress
    Cart^ Resume(Order^ order);

    // Puts an in-progress order back on hold (the screen was left without paying)
    void Release(Order^ order);
};

// What the manager's archive button needs
public interface class IOrderArchiver
{
    int ArchivePaid();
};

public ref class OrderService : public IOrderWorkflow
{
private:
    IProductLookup^ products;
    IStockWriter^ stock;
    IOrderRepository^ orders;
    IOrderDetailRepository^ details;
    IBillRepository^ bills;
    OrderPolicies^ policies;
    IClock^ clock;

    void ValidateOrder(OrderDraft^ draft, Cart^ cart)
    {
        if (String::IsNullOrEmpty(draft->CustomerName))
            throw gcnew ArgumentException("Enter the customer name.");

        if (cart->Items->Count == 0)
            throw gcnew InvalidOperationException("Scan at least one product first.");
    }

    void ValidatePayment(OrderDraft^ draft)
    {
        PaymentMethod^ method = policies->Payments->Find(draft->PaymentMode);
        if (method == nullptr)
            throw gcnew ArgumentException("Choose a payment mode.");

        if (method->NeedsMerchant && String::IsNullOrEmpty(draft->Merchant))
            throw gcnew ArgumentException("Enter the merchant name (e.g. ABL, JazzCash) for " +
                draft->PaymentMode + " payments.");
    }

    List<OrderDetail^>^ DetailsOf(String^ orderId, Cart^ cart)
    {
        List<OrderDetail^>^ list = gcnew List<OrderDetail^>();
        for each(OrderItem ^ line in cart->Items)
            list->Add(gcnew OrderDetail(orderId, line->Item->Id, line->Item->Barcode, line->Quantity));
        return list;
    }

    // Creates the order (current == nullptr) or updates it, and stores its details
    Order^ Save(Order^ current, OrderDraft^ draft, Cart^ cart, String^ status)
    {
        bool isNew = (current == nullptr);
        Order^ order = isNew ? gcnew Order() : current;

        if (isNew)
        {
            DateTime now = clock->Now();
            order->Id = orders->NextId(now);
            order->Date = now.ToString("yyyy-MM-dd", CultureInfo::InvariantCulture);
            order->Time = now.ToString("HH:mm:ss", CultureInfo::InvariantCulture);
        }

        order->CustomerName = draft->CustomerName;
        order->CustomerContact = draft->CustomerContact;
        order->Status = status;
        order->Payment = cart->Payable();
        order->PaymentMode = draft->PaymentMode;
        order->Merchant = draft->Merchant;

        if (isNew)
            orders->Add(order);
        else
            orders->Update(order);

        details->ReplaceFor(order->Id, DetailsOf(order->Id, cart));
        return order;
    }

    void TakeFromStock(Cart^ cart)
    {
        for each(OrderItem ^ line in cart->Items)
            line->Item->Quantity -= line->Quantity;
    }

    void ReturnToStock(Cart^ cart)
    {
        for each(OrderItem ^ line in cart->Items)
            line->Item->Quantity += line->Quantity;
    }

    // Best effort to undo a payment that failed half way: the stock comes back and
    // the order goes back to in-progress. The cashier still gets the original error.
    void Rollback(Order^ order, Cart^ cart)
    {
        ReturnToStock(cart);
        order->Status = OrderStatus::InProgress;
        try
        {
            stock->Save();
            orders->Update(order);
        }
        catch (Exception^)
        {
            // nothing more can be done here
        }
    }

public:
    OrderService(IProductLookup^ products, IStockWriter^ stock, IOrderRepository^ orders,
        IOrderDetailRepository^ details, IBillRepository^ bills, OrderPolicies^ policies, IClock^ clock)
    {
        this->products = products;
        this->stock = stock;
        this->orders = orders;
        this->details = details;
        this->bills = bills;
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

    virtual Order^ Hold(Order^ current, OrderDraft^ draft, Cart^ cart)
    {
        ValidateOrder(draft, cart);
        return Save(current, draft, cart, OrderStatus::OnHold);
    }

    virtual Bill^ Pay(Order^ current, OrderDraft^ draft, Cart^ cart)
    {
        ValidateOrder(draft, cart);
        ValidatePayment(draft);

        Order^ order = Save(current, draft, cart, OrderStatus::InProgress);

        Bill^ bill = gcnew Bill();
        bill->Id = bills->NextId();
        bill->Source = order;
        bill->Lines = cart;

        TakeFromStock(cart);
        try
        {
            order->Status = OrderStatus::Paid;
            stock->Save();
            bills->Add(bill);
            orders->Update(order);
        }
        catch (Exception^)
        {
            Rollback(order, cart);
            throw;
        }
        return bill;
    }

    virtual List<Order^>^ HeldOrders()
    {
        return orders->FindByStatus(OrderStatus::OnHold);
    }

    virtual Cart^ Resume(Order^ order)
    {
        Cart^ cart = NewCart();
        for each(OrderDetail ^ detail in details->FindByOrderId(order->Id))
        {
            Product^ product = products->FindById(detail->ProductId);
            if (product == nullptr)
                throw gcnew InvalidOperationException("Product " + detail->ProductId.ToString() +
                    " of this order is no longer in Product.txt.");

            if (!cart->Add(product, detail->Quantity))
                throw gcnew InvalidOperationException("Not enough stock of " + product->Acronym +
                    " to resume this order.");
        }

        order->Status = OrderStatus::InProgress;
        orders->Update(order);
        return cart;
    }

    virtual void Release(Order^ order)
    {
        order->Status = OrderStatus::OnHold;
        orders->Update(order);
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
