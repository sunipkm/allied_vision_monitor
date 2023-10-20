#pragma once
#include <iostream>
#include "imgui/imgui.h"
#include <stdio.h>
#include <math.h>
#include <chrono>
#include <mutex>
#include <thread>

#include <alliedcam.h>

#include "string_format.hpp"

#include "imagetexture.hpp"

class CaptureStat
{
private:
    std::chrono::steady_clock::time_point last;
    bool firstrun = true;
    double avg = 0, avg2 = 0;
    uint64_t count = 0;
    std::mutex mtx;

    void update_avg(double period)
    {
        uint64_t ncount = count++;
        avg = (avg * ncount + period) / (count);
        avg2 = (avg2 * ncount + period * period) / (count);
    }

public:
    void reset()
    {
        std::lock_guard<std::mutex> lock(mtx);
        firstrun = true;
        avg = 0;
        avg2 = 0;
        count = 0;
    }

    void update()
    {
        std::lock_guard<std::mutex> lock(mtx);
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
    }

    void get_stats(double &avg, double &stddev)
    {
        avg = this->avg;
        stddev = sqrt(avg2 - avg * avg);
    }
};

class CameraInfo
{
public:
    std::string idstr;
    std::string name;
    std::string model;
    std::string serial;

    CameraInfo(CameraInfo &other)
    {
        idstr = other.idstr;
        name = other.name;
        model = other.model;
        serial = other.serial;
    }

    CameraInfo()
    {
        idstr = "";
        name = "";
        model = "";
        serial = "";
    }

    CameraInfo(CameraInfo *other)
    {
        idstr = other->idstr;
        name = other->name;
        model = other->model;
        serial = other->serial;
    }

    CameraInfo(VmbCameraInfo_t info)
    {
        idstr = info.cameraIdString;
        name = info.cameraName;
        model = info.modelName;
        serial = info.serialString;
    }
};

class CharContainer
{
private:
    char *strdup(const char *str)
    {
        int len = strlen(str);
        char *out = new char[len + 1];
        strcpy(out, str);
        return out;
    }

public:
    char **arr = nullptr;
    int narr = 0;
    int selected;

    ~CharContainer()
    {
        if (arr)
        {
            for (int i = 0; i < narr; i++)
            {
                delete[] arr[i];
            }
            delete[] arr;
        }
    }

    CharContainer()
    {
        arr = nullptr;
        narr = 0;
        selected = -1;
    }

    CharContainer(const char **arr, int narr)
    {
        this->arr = new char *[narr];
        this->narr = narr;
        this->selected = -1;
        for (int i = 0; i < narr; i++)
        {
            this->arr[i] = strdup(arr[i]);
        }
    }

    CharContainer(const char **arr, int narr, const char *key)
    {
        this->arr = new char *[narr];
        this->narr = narr;
        this->selected = find_idx(key);
        for (int i = 0; i < narr; i++)
        {
            this->arr[i] = strdup(arr[i]);
        }
    }

    int find_idx(const char *str)
    {
        int res = -1;
        for (int i = 0; i < narr; i++)
        {
            if (strcmp(arr[i], str) == 0)
                res = i;
        }
        return res;
    }
};

class TempSensors
{
private:
    char **arr = nullptr;
    VmbBool_t *supported = nullptr;
    VmbUint32_t narr = 0;
    uint32_t cadence_ms = 100;
    AlliedCameraHandle_t handle = nullptr;
    bool running = false;
    bool errored = true;
    std::thread opthread;
    std::mutex mtx;
    std::vector<double> temps;

    static void ThreadFcn(TempSensors *self)
    {
        while (self->running)
        {
            self->update();
            std::this_thread::sleep_for(std::chrono::milliseconds(self->cadence_ms));
        }
    }

    void update()
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (arr == nullptr || supported == nullptr || temps.size() != narr)
            return;
        VmbError_t err;
        for (int i = 0; i < narr; i++)
        {
            temps[i] = -280; // set to invalid temperature
            if (supported[i])
            {
                err = allied_set_temperature_src(handle, arr[i]);
                if (err != VmbErrorSuccess)
                {
                    continue;
                }
                err = allied_get_temperature(handle, &temps[i]);
                if (err != VmbErrorSuccess)
                {
                    temps[i] = -280;
                }
            }
        }
    }

public:
    TempSensors(AlliedCameraHandle_t handle, uint32_t cadence_ms = 50) // default to 50 ms cadence
    {
        this->handle = handle;
        this->cadence_ms = cadence_ms;
        VmbError_t err = allied_get_temperature_src_list(handle, &arr, &supported, &narr);
        if (err == VmbErrorSuccess)
        {
            temps.resize(narr);
            errored = false;
            running = true;
            opthread = std::thread(ThreadFcn, this);
            return;
        }
        std::cerr << "Could not get temperature sensor list: " << allied_strerr(err) << std::endl;
        if (arr)
            free(arr);
        if (supported)
            free(supported);
        arr = nullptr;
        supported = nullptr;
    }

    ~TempSensors()
    {
        if (!errored)
        {
            running = false;
            opthread.join();
        }
        if (arr)
            free(arr);
        if (supported)
            free(supported);
    }

    const char **get_temps(std::vector<double> &temps)
    {
        temps = this->temps;
        return (const char **)arr;
    }
};

class ImageDisplay
{
private:
    CameraInfo info;
    std::string title;
    bool opened;
    AlliedCameraHandle_t handle = nullptr;
    std::string errmsg;
    Image img;
    CaptureStat stat;
    CharContainer *pixfmts = nullptr;
    CharContainer *adcrates = nullptr;
    TempSensors *tempsensors = nullptr;

    ImVec2 render_size(uint32_t swid, uint32_t shgt)
    {
        ImVec2 avail_size = ImGui::GetContentRegionAvail();
        float wid = avail_size[0]; // get window width
        float hgt = avail_size[1]; // get window height
        ImVec2 out = ImVec2(
            wid,
            round((float)shgt / (float)swid * wid) // calculate height
        );
        if (out[1] > hgt)
        {
            out[1] = hgt;
            out[0] = round((float)swid / (float)shgt * hgt); // calculate width
        }
        return out;
    }

public:
    bool show;

    ImageDisplay(const CameraInfo &info)
    {
        show = false;
        this->info = info;
        title = info.name + " [" + info.serial + "]";
        opened = false;
        VmbError_t err = allied_open_camera(&handle, info.idstr.c_str());
        if (err != VmbErrorSuccess)
        {
            errmsg = "Could not open camera: " + std::string(allied_strerr(err));
            return;
        }
        err = allied_alloc_framebuffer(handle, 5);
        if (err != VmbErrorSuccess)
        {
            update_err(string_format("Could not allocate memory for %d frames", 5), err);
            return;
        }
        char *key = nullptr;
        char **arr = nullptr;
        VmbBool_t *supported = nullptr;
        VmbUint32_t narr = 0;
        err = allied_get_image_format(handle, &key);
        if (err == VmbErrorSuccess)
        {
            err = allied_get_image_format_list(handle, &arr, &supported, &narr);
            if (err == VmbErrorSuccess)
            {
                pixfmts = new CharContainer((const char **)arr, narr, key);
                free(arr);
                free(supported);
                narr = 0;
            }
            else
            {
                update_err("Could not get image format list", err);
            }
        }
        else
        {
            update_err("Could not get image format", err);
        }
        err = allied_get_image_format(handle, &key);
        if (err == VmbErrorSuccess)
        {
            err = allied_get_sensor_bit_depth_list(handle, &arr, &supported, &narr);
            if (err == VmbErrorSuccess)
            {
                adcrates = new CharContainer((const char **)arr, narr, key);
                free(arr);
                free(supported);
                narr = 0;
            }
            else
            {
                update_err("Could not get sensor bit depth list", err);
            }
        }
        else
        {
            update_err("Could not get image format", err);
        }
        tempsensors = new TempSensors(handle);
        opened = true;
    }

    void display()
    {
        static bool size_changed = true;
        static bool ofst_changed = true;
        static bool exp_changed = true;
        static bool pressed_start = false;
        static bool pressed_stop = false;

        static int swid, shgt;
        static int ofx, ofy;
        static double expmin, expmax, expstep;
        static double currexp;
        static double frate;

        ImGui::SetNextWindowSizeConstraints(ImVec2(512, 512), ImVec2(INFINITY, INFINITY));
        const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
        if (show && ImGui::Begin(title.c_str(), &show))
        {
            if (!opened)
            {
                ImGui::TextUnformatted(errmsg.c_str());
            }
            else
            {
                {
                    std::vector<double> temps;
                    const char **srcs = tempsensors->get_temps(temps);
                    ImGui::Text("Temperatures:");
                    for (int i = 0; i < temps.size(); i++)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s: %5.2f C", srcs[i], temps[i]);
                    }
                    ImGui::Separator();
                }
                VmbError_t err;
                bool capturing = allied_camera_acquiring(handle) || allied_camera_streaming(handle);
                // Select pixel format and ADC bpp
                if (pixfmts != nullptr && adcrates != nullptr)
                {
                    ImGui::Text("Pixel Format:");
                    ImGui::SameLine();
                    ImGui::PushItemWidth(TEXT_BASE_WIDTH * 10);
                    int sel = pixfmts->selected;
                    if (ImGui::Combo(("##pixfmt" + info.idstr).c_str(), &sel, pixfmts->arr, pixfmts->narr))
                    {
                        if (!capturing)
                        {
                            // pixfmts->selected = sel;
                            err = allied_set_image_format(handle, pixfmts->arr[sel]);
                            update_err("Set image format", err);
                            char *key = nullptr;
                            err = allied_get_image_format(handle, &key);
                            if (err == VmbErrorSuccess && key != nullptr && (sel = pixfmts->find_idx(key)) != -1)
                            {
                                // all good
                                pixfmts->selected = sel;
                            }
                            else
                            {
                                update_err("Could not get image format", err);
                            }
                        } // don't change if capturing
                    }
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    ImGui::Text("ADC BPP:");
                    ImGui::SameLine();
                    ImGui::PushItemWidth(TEXT_BASE_WIDTH * 8);
                    if (ImGui::Combo(("##adcbpp" + info.idstr).c_str(), &adcrates->selected, adcrates->arr, adcrates->narr))
                    {
                        if (!capturing)
                        {
                            err = allied_set_sensor_bit_depth(handle, adcrates->arr[sel]);
                            update_err("Set sensor bit depth", err);
                            char *key = nullptr;
                            err = allied_get_sensor_bit_depth(handle, &key);
                            if (err == VmbErrorSuccess && key != nullptr && (sel = adcrates->find_idx(key)) != -1)
                            {
                                // all good
                                adcrates->selected = sel;
                            }
                            else
                            {
                                update_err("Could not get sensor bit depth", err);
                            }
                        }
                    }
                    ImGui::PopItemWidth();
                }
                // set width + height
                {
                    if (size_changed)
                    {
                        VmbInt64_t width, height;
                        err = allied_get_image_size(handle, &width, &height);
                        update_err("Size changed", err);
                        err = allied_get_acq_framerate(handle, &frate);
                        update_err("Get framerate", err);
                        swid = width;
                        shgt = height;
                        size_changed = false;
                    }
                    ImGui::Text("Image Size:");
                    ImGui::SameLine();
                    ImGui::PushItemWidth(TEXT_BASE_WIDTH * 5);
                    ImGui::InputInt(("##width" + info.idstr).c_str(), &swid, 0, 0, capturing ? ImGuiInputTextFlags_ReadOnly : 0);
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    ImGui::Text(" x ");
                    ImGui::SameLine();
                    ImGui::PushItemWidth(TEXT_BASE_WIDTH * 5);
                    ImGui::InputInt(("##height" + info.idstr).c_str(), &shgt, 0, 0, capturing ? ImGuiInputTextFlags_ReadOnly : 0);
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("Update##Size" + info.idstr).c_str()) && !capturing)
                    {
                        size_changed = true;
                        err = allied_set_image_size(handle, swid, shgt);
                        if (err != VmbErrorSuccess)
                        {
                            errmsg = string_format("Could not set image size to %u x %u: ", swid, shgt) + std::string(allied_strerr(err));
                        }
                    }
                }
                // set offset
                {
                    if (ofst_changed)
                    {
                        VmbInt64_t width, height;
                        err = allied_get_image_ofst(handle, &width, &height);
                        ofx = width;
                        ofy = height;
                        ofst_changed = false;
                    }
                    ImGui::Text("Image Offset:");
                    ImGui::SameLine();
                    ImGui::PushItemWidth(TEXT_BASE_WIDTH * 5);
                    ImGui::InputInt(("##ofstx" + info.idstr).c_str(), &ofx, 0, 0, capturing ? ImGuiInputTextFlags_ReadOnly : 0);
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    ImGui::Text(" x ");
                    ImGui::SameLine();
                    ImGui::PushItemWidth(TEXT_BASE_WIDTH * 5);
                    ImGui::InputInt(("##ofsty" + info.idstr).c_str(), &ofy, 0, 0, capturing ? ImGuiInputTextFlags_ReadOnly : 0);
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("Update##Ofst" + info.idstr).c_str()) && !capturing)
                    {
                        ofst_changed = true;
                        err = allied_set_image_ofst(handle, ofx, ofy);
                        if (err != VmbErrorSuccess)
                        {
                            errmsg = "Could not set image offset: " + std::string(allied_strerr(err));
                        }
                    }
                }
                // set exposure
                {
                    if (exp_changed)
                    {
                        err = allied_get_exposure_range_us(handle, &expmin, &expmax, &expstep);
                        update_err("Get exposure range", err);
                        err = allied_get_exposure_us(handle, &currexp);
                        update_err("Get exposure", err);
                        exp_changed = false;
                    }
                    ImGui::PushItemWidth(TEXT_BASE_WIDTH * 25);
                    if (ImGui::InputDouble(("Exposure (us)##" + info.idstr).c_str(), &currexp, expstep, ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        if (currexp < expmin)
                            currexp = expmin;
                        if (currexp > expmax)
                            currexp = expmax;
                    }
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("Update##Exposure" + info.idstr).c_str()))
                    {
                        if (currexp < expmin)
                            currexp = expmin;
                        if (currexp > expmax)
                            currexp = expmax;
                        err = allied_set_exposure_us(handle, currexp);
                        update_err("Update exposure", err);
                        exp_changed = true;
                        stat.reset();
                    }
                }
                // Start/stop capture
                if (!capturing)
                {
                    if (pressed_stop)
                        pressed_stop = false;
                    if (ImGui::Button(("Start Capture##" + info.idstr).c_str()) && !pressed_start)
                    {
                        pressed_start = true;
                        stat.reset();
                        err = allied_start_capture(handle, &Callback, (void *)this); // set the callback here
                        update_err("Start capture", err);
                    }
                }
                else
                {
                    if (pressed_start)
                        pressed_start = false;
                    if (ImGui::Button(("Stop Capture##" + info.idstr).c_str()) && !pressed_stop)
                    {
                        pressed_stop = true;
                        err = allied_stop_capture(handle);
                        update_err("Stop capture", err);
                    }
                }
                ImGui::Separator();
                // Capture stats display
                double avg, std;
                stat.get_stats(avg, std);
                ImGui::Text(
                    "Frame Time: %.3f +/- %.6f ms", avg * 1e-3, std * 1e-3);
                ImGui::Text("Frame Rate: %.3f FPS | Expected max: %.3f FPS", 1e6 / avg, frate);
                ImGui::Separator();
                // Error message display
                ImGui::Text("Last error: %s", errmsg.c_str());
                if (ImGui::Button(("Clear##" + info.idstr).c_str()))
                {
                    errmsg = "";
                }
                ImGui::Separator();
                // Image Display
                GLuint texture = 0;
                uint32_t width = 0, height = 0;
                if (show)
                {
                    img.get_texture(texture, width, height);
                }
                ImGui::Text("ViewFinder | %u x %u", width, height);
                if (show)
                {
                    ImGui::Image((void *)(intptr_t)texture, render_size(width, height));
                }
                ImGui::End();
            }
        }
    }

    void update_err(const char *where, VmbError_t err)
    {
        if (err != VmbErrorSuccess)
        {
            errmsg = std::string(where) + ": " + std::string(allied_strerr(err));
        }
    }

    void update_err(std::string where, VmbError_t err)
    {
        if (err != VmbErrorSuccess)
        {
            errmsg = where + ": " + std::string(allied_strerr(err));
        }
    }

    void cleanup()
    {
        if (opened)
        {
            allied_stop_capture(handle);  // just stop capture...
            allied_close_camera(&handle); // close the camera
            opened = false;
        }
    }

    ~ImageDisplay()
    {
        cleanup();
        if (pixfmts != nullptr)
            delete pixfmts;
        if (adcrates != nullptr)
            delete adcrates;
        if (tempsensors != nullptr)
            delete tempsensors;
    }

    static void Callback(const AlliedCameraHandle_t handle, const VmbHandle_t stream, VmbFrame_t *frame, void *user_data)
    {
        assert(user_data);
        ImageDisplay *self = (ImageDisplay *)user_data;
        self->stat.update();
        self->img.update(frame);
    }
};
