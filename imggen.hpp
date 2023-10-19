#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <VmbC/VmbC.h>
#include <thread>

#include <chrono>

#include "imagetexture.hpp"

class ImageGenerator
{
private:
    uint32_t width;
    uint32_t height;
    VmbPixelFormat_t pixelFormat;
    ssize_t elem_size;
    ssize_t rmax;
    uint8_t *data;
    Image *img;
    uint32_t sleep_us = 100;
    std::thread thread;
    bool running = false;
    std::chrono::steady_clock::time_point last;
    bool firstrun = true;
    double avg, avg2;
    uint64_t count;

public:
    ImageGenerator(uint32_t width, uint32_t height, VmbPixelFormat_t pixelFormat, Image *img)
    {
        this->width = width;
        this->height = height;
        this->pixelFormat = pixelFormat;
        switch (pixelFormat)
        {
        case VmbPixelFormatMono8:
            elem_size = 1;
            rmax = 0xff;
            break;
        case VmbPixelFormatMono10:
            elem_size = 2;
            rmax = 0x3ff;
            break;
        case VmbPixelFormatMono12:
            elem_size = 2;
            rmax = 0xfff;
            break;
        case VmbPixelFormatMono14:
            elem_size = 2;
            rmax = 0x3fff;
            break;
        case VmbPixelFormatMono16:
            elem_size = 2;
            rmax = 0xffff;
            break;
        case VmbPixelFormatRgb8:
            elem_size = 3;
            rmax = 0xff;
            break;
        default:
            elem_size = 4;
            rmax = 0xff;
            break;
        }
        data = new uint8_t[width * height * elem_size];
        this->img = img;
    }

    void set_sleep(uint32_t sleep_us)
    {
        this->sleep_us = sleep_us;
        printf("Sleep: %u us\n", sleep_us);
    }

    ~ImageGenerator()
    {
        if (running)
        {
            running = false;
            thread.join();
        }
        delete[] data;
    }

    void start()
    {
        running = true;
        firstrun = true;
        avg = 0;
        avg2 = 0;
        count = 0;
        thread = std::thread(ImageGenerator::generate_fn, this);
    }

    void join()
    {
        running = false;
        thread.join();
    }

    bool isrunning()
    {
        return running;
    }

    void get_stats(double &avg, double &stddev)
    {
        avg = this->avg;
        stddev = sqrt(avg2 - avg * avg);
    }

private:
    void generate()
    {
        if (firstrun)
        {
            last = std::chrono::steady_clock::now();
            firstrun = false;
        }
        else
        {
            auto now = std::chrono::steady_clock::now();
            auto diff = now - last;
            auto diff_us = std::chrono::duration_cast<std::chrono::microseconds>(diff);
            double period = diff_us.count();
            update_avg(period);
            last = now;
        }
        if (elem_size % 2 == 0)
        {
            uint16_t *ptr = (uint16_t *)data;
            for (uint32_t i = 0; i < width * height * (elem_size / 2); i++) // 2 or 4
            {
                *ptr++ = (uint16_t)(rand() % rmax);
            }
        }
        else
        {
            uint8_t *ptr = data;
            for (uint32_t i = 0; i < width * height * elem_size; i++) // 1 or 3
            {
                *ptr++ = (uint8_t)(rand() % rmax);
            }
        }
        VmbFrame_t frame = get_frame();
        img->update(&frame);
    }

    static void generate_fn(ImageGenerator *self)
    {
        while (self->running)
        {
            self->generate();
            std::this_thread::sleep_for(std::chrono::microseconds(self->sleep_us));
        }
    }

    uint8_t *clone()
    {
        uint8_t *clone = new uint8_t[width * height * elem_size];
        memcpy(clone, data, width * height * elem_size);
        return clone;
    }

    VmbFrame_t get_frame()
    {
        VmbFrame_t frame;

        frame.buffer = data;
        frame.bufferSize = width * height * elem_size;
        frame.width = width;
        frame.height = height;
        frame.pixelFormat = pixelFormat;
        frame.receiveStatus = VmbFrameStatusComplete;

        return frame;
    }

    void update_avg(double period)
    {
        uint64_t ncount = count++;
        avg = (avg * ncount + period) / (count);
        avg2 = (avg2 * ncount + period * period) / (count);
    }
};
