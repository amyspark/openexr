//
// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Contributors to the OpenEXR Project.
//

#ifdef NDEBUG
#    undef NDEBUG
#endif

#include "compareB44.h"
#include "compareDwa.h"

#include <IlmThread.h>
#include <ImathRandom.h>
#include <ImfArray.h>
#include <ImfRgbaFile.h>
#include <ImfThreading.h>
#include <assert.h>
#include <stdio.h>
#include <string>

using namespace OPENEXR_IMF_NAMESPACE;
using namespace std;
using namespace IMATH_NAMESPACE;

namespace
{

Rand32 rand1 (27);

void
fillPixels (Array2D<Rgba>& pixels, int w, int h)
{
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            Rgba& p = pixels[y][x];

            p.r = 0.5 + 0.5 * sin (0.1 * x + 0.1 * y);
            p.g = 0.5 + 0.5 * sin (0.1 * x + 0.2 * y);
            p.b = 0.5 + 0.5 * sin (0.1 * x + 0.3 * y);
            p.a = (p.r + p.b + p.g) / 3.0;
        }
    }
}

void
writeReadRGBA (
    const char           fileName[],
    int                  width,
    int                  height,
    const Array2D<Rgba>& p1,
    RgbaChannels         channels,
    LineOrder            lorder,
    Compression          comp)
{
    //
    // Save the selected channels of RGBA image p1; save the
    // scan lines in the specified order.  Read the image back
    // from the file, and compare the data with the orignal.
    //

    cout << "channels " << ((channels & WRITE_R) ? "R" : "")
         << ((channels & WRITE_G) ? "G" : "")
         << ((channels & WRITE_B) ? "B" : "")
         << ((channels & WRITE_A) ? "A" : "") << ", line order " << lorder
         << ", compression " << comp << endl;

    Header header (width, height);
    header.lineOrder ()   = lorder;
    header.compression () = comp;

    {
        remove (fileName);
        RgbaOutputFile out (fileName, header, channels);
        out.setFrameBuffer (&p1[0][0], 1, width);

        int numLeft  = height;
        int numWrite = 1;

        //
        // Iterate over all scanlines, and write them out in random-size chunks
        //

        while (numLeft)
        {
            numWrite = int (rand1.nextf () * numLeft + 0.5f);
            out.writePixels (numWrite);
            numLeft -= numWrite;
        }
    }

    {
        RgbaInputFile in (fileName);
        const Box2i&  dw = in.dataWindow ();

        int w  = dw.max.x - dw.min.x + 1;
        int h  = dw.max.y - dw.min.y + 1;
        int dx = dw.min.x;
        int dy = dw.min.y;

        Array2D<Rgba> p2 (h, w);
        in.setFrameBuffer (&p2[-dy][-dx], 1, w);
        in.readPixels (dw.min.y, dw.max.y);

        assert (in.displayWindow () == header.displayWindow ());
        assert (in.dataWindow () == header.dataWindow ());
        assert (in.pixelAspectRatio () == header.pixelAspectRatio ());
        assert (in.screenWindowCenter () == header.screenWindowCenter ());
        assert (in.screenWindowWidth () == header.screenWindowWidth ());
        assert (in.lineOrder () == header.lineOrder ());
        assert (in.compression () == header.compression ());
        assert (in.channels () == channels);

        if (in.compression () == B44_COMPRESSION ||
            in.compression () == B44A_COMPRESSION)
        {
            compareB44 (w, h, p1, p2, channels);
        }
        else if (
            in.compression () == DWAA_COMPRESSION ||
            in.compression () == DWAB_COMPRESSION)
        {
            compareDwa (w, h, p1, p2, channels);
        }
        else
        {
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    if (channels & WRITE_R)
                        assert (p2[y][x].r == p1[y][x].r);
                    else
                        assert (p2[y][x].r == 0);

                    if (channels & WRITE_G)
                        assert (p2[y][x].g == p1[y][x].g);
                    else
                        assert (p2[y][x].g == 0);

                    if (channels & WRITE_B)
                        assert (p2[y][x].b == p1[y][x].b);
                    else
                        assert (p2[y][x].b == 0);

                    if (channels & WRITE_A)
                        assert (p2[y][x].a == p1[y][x].a);
                    else
                        assert (p2[y][x].a == 1);
                }
            }
        }
    }

    remove (fileName);
}

} // namespace

void
testRgbaThreading (const std::string& tempDir)
{
    try
    {
        cout << "Testing setGlobalThreadCount()" << endl;

        if (!ILMTHREAD_NAMESPACE::supportsThreads ())
        {
            cout << "   Threading not supported!" << endl << endl;
            return;
        }

        for (int i = 0; i < 10000; i++)
        {
            int numThreads = int (rand1.nextf () * 32 + 0.5f);
            setGlobalThreadCount (numThreads);

            if (i % 2000 == 0) cout << "." << flush;
        }

        cout << "\nok\n" << endl;

        cout << "Testing multi-threaded writing of scanlines\n"
                "in random-sized blocks"
             << endl;

        const int W = 237;
        const int H = 119;

        Array2D<Rgba> p1 (H, W);
        fillPixels (p1, W, H);

        for (int n = 0; n <= 8; n++)
        {
            int numThreads = (n * 3) % 8;

            setGlobalThreadCount (numThreads);
            cout << "number of threads: " << globalThreadCount () << endl;

            for (int comp = 0; comp < NUM_COMPRESSION_METHODS; ++comp)
            {
                for (int lorder = 0; lorder < RANDOM_Y; ++lorder)
                {
                    writeReadRGBA (
                        (tempDir + "imf_test_rgba.exr").c_str (),
                        W,
                        H,
                        p1,
                        WRITE_RGBA,
                        LineOrder (lorder),
                        Compression (comp));

                    writeReadRGBA (
                        (tempDir + "imf_test_rgba.exr").c_str (),
                        W,
                        H,
                        p1,
                        WRITE_RGB,
                        LineOrder (lorder),
                        Compression (comp));

                    writeReadRGBA (
                        (tempDir + "imf_test_rgba.exr").c_str (),
                        W,
                        H,
                        p1,
                        WRITE_A,
                        LineOrder (lorder),
                        Compression (comp));

                    writeReadRGBA (
                        (tempDir + "imf_test_rgba.exr").c_str (),
                        W,
                        H,
                        p1,
                        RgbaChannels (WRITE_R | WRITE_B),
                        LineOrder (lorder),
                        Compression (comp));
                }
            }
        }

        cout << "ok\n" << endl;
    }
    catch (const std::exception& e)
    {
        cerr << "ERROR -- caught exception: " << e.what () << endl;
        assert (false);
    }
}
