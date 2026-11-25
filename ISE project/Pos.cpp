// =============================================================================
// Pos.cpp - composition root: builds the objects and starts the program
//   PosFactory is the ONLY place that names concrete classes. To change a policy
//   (stock rule, payment methods, units, camera, files ...) change one line here;
//   nothing else in the program is touched.
// =============================================================================

#include "Common.h"
#include "Barcode.h"
#include "Domain.h"
#include "Persistence.h"
#include "Services.h"
#include "ManagerServices.h"
#include "Pdf.h"
#include "Camera.h"
#include "Screens.h"
#include "ManagerScreens.h"

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

    // The data files
    ProductStore^ CreateProducts() { return gcnew ProductStore(Files::ProductFile, CreateUnits()); }
    FileCategoryRepository^ CreateCategories() { return gcnew FileCategoryRepository(Files::CategoryFile); }
    FileSeasonRepository^ CreateSeasons() { return gcnew FileSeasonRepository(Files::SeasonFile); }
    FileOrderRepository^ CreateOrders() { return gcnew FileOrderRepository(Files::OrderFile); }
    FileOrderDetailRepository^ CreateDetails() { return gcnew FileOrderDetailRepository(Files::OrderDetailFile); }
    FileBillRepository^ CreateBills() { return gcnew FileBillRepository(Files::BillFile, Files::BillDetailFile); }
    FileApprovalRepository^ CreateApprovals() { return gcnew FileApprovalRepository(Files::ApprovalFile); }

    IPdfExporter^ CreateExporter()
    {
        return gcnew SimplePdfExporter();
    }

    // Business rules. Stock: LessThanAvailableRule (as the requirement says)
    // or UpToAvailableRule (the last unit may be sold too).
    OrderPolicies^ CreatePolicies()
    {
        return gcnew OrderPolicies(
            gcnew LessThanAvailableRule(),
            gcnew ProductFilePricing(),
            CreatePayments(),
            gcnew SeasonCalendar(CreateSeasons()));
    }

    // No camera (or a camera that cannot be opened): the screens still open and the
    // barcode number can be typed (or a USB scanner can be used) instead
    ScannerControl^ CreateScanner()
    {
        IFrameSource^ source = nullptr;
        String^ problem = nullptr;
        try
        {
            source = gcnew CameraScanner();
        }
        catch (Exception^ ex)
        {
            problem = ex->Message;
        }

        if (source == nullptr)
            source = gcnew NoCamera();

        ScannerControl^ scanner = gcnew ScannerControl(source, gcnew Barcode());
        if (problem != nullptr)
            scanner->ShowMessage(problem + " Type the barcode number below.");
        return scanner;
    }

    IOrderWorkflow^ CreateWorkflow(ProductStore^ store)
    {
        FileBillRepository^ bills = CreateBills();
        OrderStores^ stores = gcnew OrderStores(CreateOrders(), CreateDetails(), bills, bills, CreateApprovals());
        return gcnew OrderService(store, store, stores, CreatePolicies(), gcnew SystemClock());
    }

    ICategoryService^ CreateCategoryService()
    {
        return gcnew CategoryService(CreateCategories());
    }

    IProductManager^ CreateProductManager(ProductStore^ store)
    {
        return gcnew ProductService(store, store, store, CreateUnits(), CreateCategories());
    }

    Form^ CreateOrderScreen(bool openFirst)
    {
        // products first: a bad Product.txt is reported before the camera starts
        ProductStore^ store = CreateProducts();
        IOrderWorkflow^ workflow = CreateWorkflow(store);
        ScannerControl^ scanner = CreateScanner();
        return gcnew OrderForm(store, workflow, scanner, CreateExporter(), openFirst);
    }

    Form^ CreateProductScreen()
    {
        ProductStore^ store = CreateProducts();
        ScannerControl^ scanner = CreateScanner();
        return gcnew ProductForm(store, scanner);
    }

public:
    // Manager login. For now one fixed account; change it here (or write a class that
    // reads users from a file or a database) without touching any screen.
    IAuthenticator^ CreateAuthenticator()
    {
        return gcnew FixedCredentialAuthenticator("admin", "admin");
    }

    IOrderArchiver^ CreateArchiver()
    {
        return gcnew OrderArchiver(CreateOrders());
    }

    // The main menu asks for a screen by name
    virtual Form^ Create(String^ screen)
    {
        if (screen == ScreenNames::Order)
            return CreateOrderScreen(false);

        if (screen == ScreenNames::OpenOrder)
            return CreateOrderScreen(true);

        if (screen == ScreenNames::Product)
            return CreateProductScreen();

        if (screen == ScreenNames::PrintBill)
            return gcnew BillListForm(CreateBills(), CreateExporter());

        if (screen == ScreenNames::AddProduct)
        {
            ProductStore^ store = CreateProducts();
            return gcnew ProductEditForm(CreateProductManager(store), CreateCategoryService(), gcnew Barcode(), nullptr);
        }

        if (screen == ScreenNames::Inventory)
        {
            ProductStore^ store = CreateProducts();
            return gcnew InventoryForm(CreateProductManager(store), CreateCategoryService(), gcnew Barcode());
        }

        if (screen == ScreenNames::Categories)
            return gcnew CategoryForm(CreateCategoryService());

        if (screen == ScreenNames::Seasons)
            return gcnew SeasonForm(gcnew SeasonService(CreateSeasons()));

        if (screen == ScreenNames::Approvals)
            return gcnew ApprovalForm(gcnew ApprovalService(CreateApprovals(), CreateOrders()));

        if (screen == ScreenNames::Reports)
        {
            ProductStore^ store = CreateProducts();
            return gcnew ReportForm(gcnew ReportService(CreateOrders(), CreateDetails(), CreateBills(), store),
                CreateExporter());
        }

        throw gcnew ArgumentException("Unknown screen: " + screen);
    }
};

[STAThread]
int main(array<String^>^ args)
{
    Application::EnableVisualStyles();
    Application::SetCompatibleTextRenderingDefault(false);

    PosFactory^ factory = gcnew PosFactory();
    Application::Run(gcnew MenuForm(factory, factory->CreateArchiver(), factory->CreateAuthenticator()));
    return 0;
}
