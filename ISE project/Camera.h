#pragma once

// =============================================================================
// Camera.h - camera access and the reusable scanner control
//   IFrameSource     where pictures come from (camera, video file, test images ...)
//   CameraScanner    the AForge implementation (first camera of the PC)
//   ScannerControl   live preview + barcode detection, put it on any form
//   Depends on       Common.h, Barcode.h (IBarcodeReader only) and the AForge
//                    references of the project
// =============================================================================

#include "Common.h"
#include "Barcode.h"

using namespace AForge::Video;
using namespace AForge::Video::DirectShow;

// Raised by the scanner control when a valid barcode is read
public delegate void BarcodeHandler(String^ payload);

public interface class IFrameSource
{
    void Start();
    void Stop();
    Bitmap^ GetFrame();         // a copy the caller owns, nullptr before the first frame
};

// Talks to the first camera and keeps its latest frame
public ref class CameraScanner : public IFrameSource
{
private:
    literal int MaxFrameWidth = 1280;       // bigger modes only make everything slower

    VideoCaptureDevice^ source;
    Bitmap^ lastFrame;
    Object^ sync;

    // Called by AForge on its own thread for every frame
    void OnNewFrame(Object^ sender, NewFrameEventArgs^ e)
    {
        Bitmap^ frame = (Bitmap^)e->Frame->Clone();

        System::Threading::Monitor::Enter(sync);
        try
        {
            Bitmap^ old = lastFrame;
            lastFrame = frame;
            if (old != nullptr) delete old;
        }
        finally
        {
            System::Threading::Monitor::Exit(sync);
        }
    }

    // Biggest mode up to MaxFrameWidth: thin bars stay readable at a normal distance
    void ChooseResolution()
    {
        VideoCapabilities^ best = nullptr;
        for each(VideoCapabilities ^ cap in source->VideoCapabilities)
        {
            if (cap->FrameSize.Width > MaxFrameWidth)
                continue;

            if (best == nullptr || cap->FrameSize.Width > best->FrameSize.Width)
                best = cap;
        }
        if (best != nullptr)
            source->VideoResolution = best;
    }

public:
    CameraScanner()
    {
        sync = gcnew Object();
        lastFrame = nullptr;

        FilterInfoCollection^ devices = gcnew FilterInfoCollection(FilterCategory::VideoInputDevice);
        if (devices->Count == 0)
            throw gcnew InvalidOperationException("No camera found.");

        source = gcnew VideoCaptureDevice(devices[0]->MonikerString);
        ChooseResolution();
        source->NewFrame += gcnew NewFrameEventHandler(this, &CameraScanner::OnNewFrame);
    }

    virtual void Start()
    {
        source->Start();
    }

    virtual void Stop()
    {
        source->SignalToStop();
        source->WaitForStop();
    }

    virtual Bitmap^ GetFrame()
    {
        Bitmap^ copy = nullptr;

        System::Threading::Monitor::Enter(sync);
        try
        {
            if (lastFrame != nullptr)
                copy = (Bitmap^)lastFrame->Clone();
        }
        finally
        {
            System::Threading::Monitor::Exit(sync);
        }
        return copy;
    }
};

// Reusable control: live preview + barcode detection.
// Put it on any form and subscribe to BarcodeScanned.
public ref class ScannerControl : public UserControl
{
private:
    literal int PreviewIntervalMs = 40;     // about 25 preview frames per second
    literal int DecodeIntervalMs = 250;     // decoding is slower: 4 times a second at most
    literal int RepeatPauseSeconds = 2;     // the same barcode stays in front of the camera

    IBarcodeReader^ reader;
    IFrameSource^ camera;
    PictureBox^ pic;
    Timer^ timer;
    Label^ info;
    DateTime pausedUntil;
    DateTime nextDecode;

public:
    event BarcodeHandler^ BarcodeScanned;

    ScannerControl(IFrameSource^ camera, IBarcodeReader^ reader)
    {
        this->camera = camera;
        this->reader = reader;
        pausedUntil = DateTime::MinValue;
        nextDecode = DateTime::MinValue;

        pic = gcnew PictureBox();
        pic->Dock = DockStyle::Fill;
        pic->BorderStyle = System::Windows::Forms::BorderStyle::FixedSingle;
        pic->BackColor = Color::Black;
        pic->SizeMode = PictureBoxSizeMode::Zoom;
        this->Controls->Add(pic);

        // Small status line under the preview: shows what the scanner sees
        info = gcnew Label();
        info->Dock = DockStyle::Bottom;
        info->Height = 20;
        info->BackColor = Color::Black;
        info->ForeColor = Color::Lime;
        info->Text = "Waiting for camera...";
        this->Controls->Add(info);

        camera->Start();

        timer = gcnew Timer();
        timer->Interval = PreviewIntervalMs;
        timer->Tick += gcnew EventHandler(this, &ScannerControl::OnTick);
        timer->Start();
    }

    // Disposed together with the form: switches the camera off
    ~ScannerControl()
    {
        timer->Stop();
        camera->Stop();
    }

    // Stops / restarts reading (the camera stays on). Use around dialogs.
    void Pause()
    {
        timer->Stop();
    }

    void Resume()
    {
        timer->Start();
    }

private:
    void ShowPreview(Bitmap^ frame)
    {
        Image^ old = pic->Image;
        pic->Image = frame;
        if (old != nullptr) delete old;
    }

    // What the scanner sees when no barcode was read
    void ShowDiagnostics(Bitmap^ frame)
    {
        String^ size = "Camera " + frame->Width.ToString() + "x" + frame->Height.ToString();
        int closest = reader->ClosestMatch();
        if (closest < Code39::NoMatch)
            info->Text = size + "  | closest match: " + closest.ToString() + " wrong modules (need 8 or less)";
        else
            info->Text = size + "  | no barcode-like bars found";
    }

    void OnTick(Object^ sender, EventArgs^ e)
    {
        Bitmap^ frame = camera->GetFrame();
        if (frame == nullptr)
            return;

        ShowPreview(frame);

        if (DateTime::Now < pausedUntil || DateTime::Now < nextDecode)
            return;
        nextDecode = DateTime::Now.AddMilliseconds(DecodeIntervalMs);

        String^ payload = reader->TryDecode(frame);
        if (payload == nullptr)
        {
            ShowDiagnostics(frame);
            return;
        }

        info->Text = "Read: " + payload;
        pausedUntil = DateTime::Now.AddSeconds(RepeatPauseSeconds);
        BarcodeScanned(payload);
    }
};
