#pragma once

// =============================================================================
// Common.h - building blocks shared by every other header
//   Responsibility : text/number formatting, data-folder lookup, allowed-name sets,
//                    clock, and the generic record file (read / write / append)
//   Depends on     : nothing
// =============================================================================

#using <System.dll>
#using <System.Drawing.dll>
#using <System.Windows.Forms.dll>

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Drawing;
using namespace System::Drawing::Imaging;
using namespace System::Globalization;
using namespace System::IO;
using namespace System::Text;
using namespace System::Text::RegularExpressions;
using namespace System::Windows::Forms;

// Number / text formatting (always '.' as decimal point, whatever the PC's region is)
public ref class Fmt abstract sealed
{
public:
    // 12.5 -> "12.5", 12 -> "12" (used inside files)
    static String^ Number(double value)
    {
        return value.ToString("0.##", CultureInfo::InvariantCulture);
    }

    // 12.5 -> "12.50" (used on screen)
    static String^ Money(double value)
    {
        return value.ToString("0.00", CultureInfo::InvariantCulture);
    }

    static double Parse(String^ text)
    {
        return Double::Parse(text, CultureInfo::InvariantCulture);
    }

    static bool IsNumber(String^ text)
    {
        double value;
        return Double::TryParse(text, NumberStyles::Float, CultureInfo::InvariantCulture, value);
    }

    // 2026-11-21 (dates inside files)
    static String^ DateText(DateTime value)
    {
        return value.ToString("yyyy-MM-dd", CultureInfo::InvariantCulture);
    }

    // 13:45:09 (times inside files)
    static String^ TimeText(DateTime value)
    {
        return value.ToString("HH:mm:ss", CultureInfo::InvariantCulture);
    }

    static DateTime ParseDate(String^ text)
    {
        return DateTime::ParseExact(text, "yyyy-MM-dd", CultureInfo::InvariantCulture);
    }

    static DateTime ParseDateTime(String^ date, String^ time)
    {
        return DateTime::ParseExact(date + " " + time, "yyyy-MM-dd HH:mm:ss", CultureInfo::InvariantCulture);
    }

    // Fields are separated by spaces, so spaces inside a value become '_'. Empty -> "-"
    static String^ Pack(String^ text)
    {
        if (String::IsNullOrEmpty(text))
            return "-";
        return text->Replace(" ", "_");
    }

    // Opposite of Pack: "-" -> empty, '_' -> space
    static String^ Unpack(String^ text)
    {
        if (text == "-")
            return "";
        return text->Replace("_", " ");
    }

    static String^ Left(String^ text, int width)
    {
        return text->PadRight(width);
    }

    static String^ Right(String^ text, int width)
    {
        return text->PadLeft(width);
    }
};

// Where the data files live
public ref class Files abstract sealed
{
private:
    static String^ folder = nullptr;

    // The folder that contains Product.txt: the working directory or the exe folder,
    // or one of their parent folders (Visual Studio runs the exe from a Debug folder,
    // while the data files usually sit in the project folder).
    static String^ Locate()
    {
        array<String^>^ roots = gcnew array<String^>
        {
            Directory::GetCurrentDirectory(), AppDomain::CurrentDomain->BaseDirectory
        };

        for each(String ^ root in roots)
        {
            DirectoryInfo^ dir = gcnew DirectoryInfo(root);
            for (int up = 0; dir != nullptr && up < 5; up++)
            {
                if (File::Exists(Path::Combine(dir->FullName, "Product.txt")))
                    return dir->FullName;
                dir = dir->Parent;
            }
        }
        return Directory::GetCurrentDirectory();
    }

public:
    static property String^ Folder
    {
        String ^ get()
        {
            if (folder == nullptr)
                folder = Locate();
            return folder;
        }
    }

    static property String^ ProductFile
    {
        String ^ get() { return Path::Combine(Folder, "Product.txt"); }
    }

    static property String^ OrderFile
    {
        String ^ get() { return Path::Combine(Folder, "Order.txt"); }
    }

    static property String^ OrderDetailFile
    {
        String ^ get() { return Path::Combine(Folder, "OrderDetail.txt"); }
    }

    static property String^ BillFile
    {
        String ^ get() { return Path::Combine(Folder, "Bill.txt"); }
    }

    static property String^ BillDetailFile
    {
        String ^ get() { return Path::Combine(Folder, "BillDetail.txt"); }
    }

    static property String^ CategoryFile
    {
        String ^ get() { return Path::Combine(Folder, "PCategory.txt"); }
    }

    static property String^ SeasonFile
    {
        String ^ get() { return Path::Combine(Folder, "Seasons.txt"); }
    }

    static property String^ ApprovalFile
    {
        String ^ get() { return Path::Combine(Folder, "Approval.txt"); }
    }
};

// The four order statuses of the requirement
public ref class OrderStatus abstract sealed
{
public:
    literal String^ InProgress = "in-progress";
    literal String^ OnHold = "on-hold";
    literal String^ Paid = "paid";
    literal String^ Archived = "archived";
};

// Payment mode names the program itself has to know (reports: cash sales)
public ref class PaymentNames abstract sealed
{
public:
    literal String^ Cash = "cash";
};

// A small set of allowed names (product units ...). Adding a name never changes
// the code that uses the set.
public ref class NameSet
{
private:
    List<String^>^ names;

public:
    NameSet(array<String^>^ initial)
    {
        names = gcnew List<String^>();
        for each(String ^ name in initial)
            names->Add(name);
    }

    void Add(String^ name)
    {
        names->Add(name);
    }

    bool Contains(String^ name)
    {
        return names->Contains(name);
    }

    array<String^>^ ToArray()
    {
        return names->ToArray();
    }
};

// The current time (an interface, so the workflow does not depend on the PC clock)
public interface class IClock
{
    DateTime Now();
};

public ref class SystemClock : public IClock
{
public:
    virtual DateTime Now()
    {
        return DateTime::Now;
    }
};

// ---------------------------------------------------------------------------
// Record + file IO. Any new record type only has to implement ToLine()
// ---------------------------------------------------------------------------
public ref class Record abstract
{
public:
    // One record -> one line of text (without the '#' end marker)
    virtual String^ ToLine() abstract;
};

public ref class RecordFile abstract sealed
{
private:
    // Writes to a temporary file first, so a crash never leaves a half-written data file
    static void WriteAtomically(String^ path, String^ text)
    {
        String^ temp = path + ".tmp";
        File::WriteAllText(temp, text);

        if (File::Exists(path))
            File::Replace(temp, path, nullptr);
        else
            File::Move(temp, path);
    }

public:
    // Reads every record of a file ('#' is the end of line marker)
    static List<String^>^ ReadLines(String^ path)
    {
        List<String^>^ lines = gcnew List<String^>();
        if (!File::Exists(path))
            return lines;

        array<String^>^ parts = File::ReadAllText(path)->Split(gcnew array<Char>{ L'#' });
        for each(String ^ part in parts)
        {
            String^ line = part->Trim();
            if (line->Length > 0)
                lines->Add(line);
        }
        return lines;
    }

    // Replaces the whole file (all or nothing)
    static void WriteLines(String^ path, List<String^>^ lines)
    {
        StringBuilder^ sb = gcnew StringBuilder();
        for each(String ^ line in lines)
            sb->AppendLine(line + "#");
        WriteAtomically(path, sb->ToString());
    }

    // Adds one line at the end of the file
    static void AppendLine(String^ path, String^ line)
    {
        File::AppendAllText(path, line + "#" + Environment::NewLine);
    }

    // Adds one record at the end of the file
    static void Append(String^ path, Record^ record)
    {
        AppendLine(path, record->ToLine());
    }

    // Next running number: the highest number found in the first 'digits'
    // characters of every record, plus one (1 for an empty / missing file)
    static int NextNumber(String^ path, int digits)
    {
        int highest = 0;
        for each(String ^ line in ReadLines(path))
        {
            String^ id = line->Split(gcnew array<Char>{ L' ' })[0];
            int value;
            if (id->Length >= digits && Int32::TryParse(id->Substring(0, digits), value) && value > highest)
                highest = value;
        }
        return highest + 1;
    }
};
