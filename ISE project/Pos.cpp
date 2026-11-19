// =============================================================================
// Pos.cpp - composition root: builds the objects and starts the program
//   PosFactory is the ONLY place that names concrete classes. To change a policy
//   (stock rule, bill layout, payment methods, units, camera ...) change one line
//   here; nothing else in the program is touched.
// =============================================================================

#include "Common.h"
#include "Barcode.h"
#include "Domain.h"
#include "Persistence.h"
#include "Services.h"
#include "Camera.h"
#include "Screens.h"

public ref class PosFactory : public IScreenFactory
{
private:
    // Allowed product units. A new unit = one more name here.
    NameSet^ CreateUnits()
    {
        return gcnew NameSet(gcnew array<String^>{ "count", "weight" });
    }

    // Accepted payment methods. A new method = one more line here.
    PaymentCatalog^ CreatePayments()
    {
        PaymentCatalog^ payments = gcnew PaymentCatalog();
        payments->Add(gcnew PaymentMethod("cash", false));
        payments->Add(gcnew PaymentMethod("card", true));
        payments->Add(gcnew PaymentMethod("wallet", true));
        return payments;
    }

    // Business rules. Stock: LessThanAvailableRule (as the requirement says)
    // or UpToAvailableRule (the last unit may be sold too).
    OrderPolicies^ CreatePolicies()
    {
        return gcnew OrderPolicies(
            gcnew LessThanAvailableRule(),
            gcnew ProductFilePricing(),
            CreatePayments());
    }

    // Bill.txt layout: OneLinePerBillFormat or OneLinePerProductFormat
    IBillFormat^ CreateBillFormat()
    {
        return gcnew OneLinePerBillFormat();
    }

    ProductStore^ CreateProducts()
    {
        return gcnew ProductStore(Files::ProductFile, CreateUnits());
    }

    ScannerControl^ CreateScanner()
    {
        return gcnew ScannerControl(gcnew CameraScanner(), gcnew Barcode());
    }

    IOrderWorkflow^ CreateWorkflow(ProductStore^ store)
    {
        return gcnew OrderService(store, store,
            gcnew FileOrderRepository(Files::OrderFile),
            gcnew FileOrderDetailRepository(Files::OrderDetailFile),
            gcnew FileBillRepository(Files::BillFile, CreateBillFormat()),
            CreatePolicies(),
            gcnew SystemClock());
    }

public:
    IOrderArchiver^ CreateArchiver()
    {
        return gcnew OrderArchiver(gcnew FileOrderRepository(Files::OrderFile));
    }

    virtual Form^ CreateOrderScreen()
    {
        // products first: a bad Product.txt is reported before the camera starts
        ProductStore^ store = CreateProducts();
        IOrderWorkflow^ workflow = CreateWorkflow(store);
        ScannerControl^ scanner = CreateScanner();
        return gcnew OrderForm(store, workflow, scanner);
    }

    virtual Form^ CreateProductScreen()
    {
        ProductStore^ store = CreateProducts();
        ScannerControl^ scanner = CreateScanner();
        return gcnew ProductForm(store, scanner);
    }

    virtual Form^ CreateBarcodeScreen()
    {
        // the store lets the barcode screen add / update products in Product.txt
        return gcnew BarcodeForm(gcnew Barcode(), CreateProducts());
    }
};

[STAThread]
int main(array<String^>^ args)
{
    Application::EnableVisualStyles();
    Application::SetCompatibleTextRenderingDefault(false);

    PosFactory^ factory = gcnew PosFactory();
    Application::Run(gcnew MenuForm(factory, factory->CreateArchiver()));
    return 0;
}
