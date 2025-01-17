#pragma once
#include <iostream>
#include "imgui/imgui.h"
#include <stdio.h>
#include <math.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <alliedcam.h>

#include "string_format.hpp"

#include "imagetexture.hpp"

#include "imgui_separator.hpp"

#define eprintlf(fmt, ...)                                                                     \
    {                                                                                          \
        fprintf(stderr, "%s:%d:%s(): " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
        fflush(stderr);                                                                        \
    }

static ImVec4 header_col = ImVec4(168.0 / 255, 21.0 / 255, 5.0 / 255, 1);

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
    size_t maxlen = 0;

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
            if (strlen(arr[i]) > maxlen)
            {
                maxlen = strlen(arr[i]);
            }
        }
    }

    CharContainer(const char **arr, int narr, const char *key)
    {
        this->arr = new char *[narr];
        this->narr = narr;
        for (int i = 0; i < narr; i++)
        {
            this->arr[i] = strdup(arr[i]);
            if (strlen(arr[i]) > maxlen)
            {
                maxlen = strlen(arr[i]);
            }
        }
        this->selected = find_idx(key);
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

#define TEMPSENSOR_RESPONSE 100

class TempSensors
{
private:
    char **arr = nullptr;
    VmbBool_t *supported = nullptr;
    VmbUint32_t narr = 0;
    uint32_t cadence_mod = 100;
    uint32_t cadence_loop = 0;
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
            std::this_thread::sleep_for(std::chrono::milliseconds(self->cadence_mod));
            for (uint32_t i = 0; i < self->cadence_loop && self->running; i++)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(TEMPSENSOR_RESPONSE));
            }
        }
    }

    void update()
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (arr == nullptr || supported == nullptr || temps.size() != narr)
            return;
        VmbError_t err;
        for (VmbUint32_t i = 0; i < narr; i++)
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
    TempSensors(AlliedCameraHandle_t handle, uint32_t cadence_ms = 1000) // default to 1 s cadence
    {
        this->handle = handle;
        this->cadence_mod = cadence_ms % TEMPSENSOR_RESPONSE;
        this->cadence_loop = cadence_ms / TEMPSENSOR_RESPONSE;
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
    CharContainer *triglines = nullptr;
    CharContainer *trigsrcs = nullptr;
    TempSensors *tempsensors = nullptr;
    DeviceHandle adio_hdl = nullptr;
    VmbInt64_t link_speed = 0;
    std::string link_speed_str = "";
    VmbInt64_t throughput = 0;
    VmbInt64_t throughput_min = 0;
    VmbInt64_t throughput_max = 0;
    unsigned char state = 0;
    bool capturing;

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
    int adio_bit = -1;

    ImageDisplay(const CameraInfo &info, const DeviceHandle &adio_hdl)
    {
        show = false;
        this->info = info;
        this->adio_hdl = adio_hdl;
        title = info.name + " [" + info.serial + "]";
        opened = false;
        capturing = false;
        // open_camera();
    }

    void open_camera(uint32_t bufsize = MIB(16))
    {
        VmbError_t err = allied_open_camera(&handle, info.idstr.c_str(), bufsize);
        if (err != VmbErrorSuccess)
        {
            errmsg = "Could not open camera: " + std::string(allied_strerr(err));
            return;
        }
        char *key = nullptr;
        char **arr = nullptr;
        VmbUint32_t narr = 0;
        err = allied_get_link_speed(handle, &link_speed);
        if (err != VmbErrorSuccess)
        {
            update_err("Could not get link speed", err);
        }
        link_speed_str = string_format("Link Speed Settings (Max: %d MBps)", link_speed / 1000 / 1000);
        err = allied_get_throughput_limit(handle, &throughput);
        if (err != VmbErrorSuccess)
        {
            update_err("Could not get throughput limit", err);
        }
        err = allied_get_throughput_limit_range(handle, &throughput_min, &throughput_max, NULL);
        if (err != VmbErrorSuccess)
        {
            update_err("Could not get throughput limit range", err);
        }
        err = allied_get_image_format(handle, (const char **)&key);
        if (err == VmbErrorSuccess)
        {
            err = allied_get_image_format_list(handle, &arr, NULL, &narr);
            if (err == VmbErrorSuccess)
            {
                pixfmts = new CharContainer((const char **)arr, narr, key);
                free(arr);
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
        err = allied_get_sensor_bit_depth(handle, (const char **)&key);
        if (err == VmbErrorSuccess)
        {
            err = allied_get_sensor_bit_depth_list(handle, &arr, NULL, &narr);
            if (err == VmbErrorSuccess)
            {
                adcrates = new CharContainer((const char **)arr, narr, (const char *)key);
                free(arr);
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
        err = allied_get_trigline(handle, (const char **)&key);
        if (err == VmbErrorSuccess)
        {
            err = allied_get_triglines_list(handle, &arr, NULL, &narr);
            if (err == VmbErrorSuccess)
            {
                triglines = new CharContainer((const char **)arr, narr, (const char *)key);
                free(arr);
                narr = 0;
            }
            else
            {
                update_err("Could not get trigger lines list", err);
            }
        }
        else
        {
            update_err("Could not get selected trigger line", err);
        }
        if (triglines != nullptr)
        {
            // set all trigger lines to output
            for (int i = 0; i < triglines->narr; i++)
            {
                char *line = triglines->arr[i];
                err = allied_set_trigline(handle, line);
                if (err != VmbErrorSuccess)
                {
                    update_err(string_format("Could not select line %s", line), err);
                }
                else
                {
                    err = allied_set_trigline_mode(handle, "Output");
                    update_err(string_format("Could not set line %s to output", line), err);
                }
            }
            err = allied_set_trigline(handle, key);
            update_err(string_format("Could not select line %s", key), err);
            // get trigger source
            err = allied_get_trigline_src(handle, (const char **)&key);
            if (err == VmbErrorSuccess)
            {
                err = allied_get_trigline_src_list(handle, &arr, NULL, &narr);
                if (err == VmbErrorSuccess)
                {
                    trigsrcs = new CharContainer((const char **)arr, narr, (const char *)key);
                    free(arr);
                    narr = 0;
                }
                else
                {
                    update_err("Could not get trigger sources list", err);
                }
            }
        }
        err = allied_queue_capture(handle, &Callback, (void *)this);
        update_err("Could not queue capture", err);
        tempsensors = new TempSensors(handle);
        opened = true;
        // std::cout << "Opened!" << std::endl;
    }

    void close_camera()
    {
        cleanup();
        if (pixfmts != nullptr)
        {
            delete pixfmts;
            pixfmts = nullptr;
        }
        if (adcrates != nullptr)
        {
            delete adcrates;
            adcrates = nullptr;
        }
        if (triglines != nullptr)
        {
            delete triglines;
            triglines = nullptr;
        }
        if (trigsrcs != nullptr)
        {
            delete trigsrcs;
            trigsrcs = nullptr;
        }
        if (tempsensors != nullptr)
        {
            delete tempsensors;
            tempsensors = nullptr;
        }
        opened = false;
        errmsg = "";
    }

    void display()
    {
        static bool bin_changed = true;
        static bool size_changed = true;
        static bool ofst_changed = true;
        static bool exp_changed = true;
        static bool luma_changed = true;
        static bool pressed_start = false;
        static bool pressed_stop = false;
        static bool led_on = true;

        static int swid, shgt, sbin;
        static int ofx, ofy;
        static double expmin, expmax, expstep;
        static double currexp;
        static double frate, frate_min, frate_max;
        static bool frate_auto = true;
        static bool frate_changed = true;
        static bool trigline_changed = true;
        static int speed = throughput / 1000 / 1000;

        ImGui::SetNextWindowSizeConstraints(ImVec2(512, 640), ImVec2(INFINITY, INFINITY));
        const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
        if (show && ImGui::Begin(title.c_str(), &show))
        {
            ImGui::PushID(title.c_str());
            if (!opened)
            {
                if (ImGui::Button("Open Camera"))
                {
                    open_camera();
                }
                ImGui::Text("Last error: %s", errmsg.c_str());
            }
            else
            {
                VmbError_t err;
                capturing = allied_camera_acquiring(handle);
                if (ImGui::Button("Close Camera"))
                {
                    close_camera();
                    goto outside;
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset Camera"))
                {
                    opened = false;
                    allied_reset_camera(&handle);
                    close_camera();
                    goto outside;
                }
                ImGui::SameLine();
                {
                    if (luma_changed)
                    {
                        luma_changed = false;
                        VmbInt64_t luma;
                        err = allied_get_indicator_luma(handle, &luma);
                        update_err("Getting indicator status", err);
                        if (err == VmbErrorSuccess)
                        {
                            led_on = luma > 0;
                        }
                    }
                    if (ImGui::Checkbox("LED", &led_on))
                    {
                        if (led_on)
                        {
                            err = allied_set_indicator_luma(handle, 10);
                        }
                        else
                        {
                            err = allied_set_indicator_luma(handle, 0);
                        }
                        update_err("Setting indicator status", err);
                        luma_changed = true;
                    }
                }
                {
                    std::vector<double> temps;
                    const char **srcs = tempsensors->get_temps(temps);
                    ImGui::Text("Temperatures:");
                    for (size_t i = 0; i < temps.size(); i++)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s: %5.2f C", srcs[i], temps[i]);
                    }
                }
                // {
                //     ImGui::PushStyleColor(ImGuiCol_Text, header_col);
                //     ImGui::TextSeparator((char *)"Image Properties");
                //     ImGui::PopStyleColor();
                // }
                if (ImGui::CollapsingHeader("Image Properties"))
                {
                    // get frame rate
                    if (frate_changed)
                    {
                        double dummy;
                        err = allied_get_acq_framerate(handle, &frate);
                        update_err("Get framerate", err);
                        err = allied_get_acq_framerate_range(handle, &frate_min, &frate_max, &dummy);
                        update_err("Get framerate range", err);
                        frate_changed = false;
                    }
                    // Select pixel format and ADC bpp
                    if (pixfmts != nullptr && adcrates != nullptr)
                    {
                        ImGui::Text("Pixel Format:");
                        ImGui::SameLine();
                        ImGui::PushItemWidth(TEXT_BASE_WIDTH * (pixfmts->maxlen + 6));
                        int sel = pixfmts->selected;
                        if (ImGui::Combo("##pixfmt", &sel, pixfmts->arr, pixfmts->narr))
                        {
                            if (!capturing)
                            {
                                // pixfmts->selected = sel;
                                err = allied_set_image_format(handle, pixfmts->arr[sel]);
                                update_err("Set image format", err);
                                char *key = nullptr;
                                err = allied_get_image_format(handle, (const char **)&key);
                                if (err == VmbErrorSuccess && key != nullptr && (sel = pixfmts->find_idx(key)) != -1)
                                {
                                    // all good
                                    pixfmts->selected = sel;
                                    frate_changed = true;
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
                        ImGui::PushItemWidth(TEXT_BASE_WIDTH * (adcrates->maxlen + 6));
                        sel = adcrates->selected;
                        if (ImGui::Combo("##adcbpp", &sel, adcrates->arr, adcrates->narr))
                        {
                            if (!capturing)
                            {
                                err = allied_set_sensor_bit_depth(handle, adcrates->arr[sel]);
                                update_err("Set sensor bit depth", err);
                                char *key = nullptr;
                                err = allied_get_sensor_bit_depth(handle, (const char **)&key);
                                if (err == VmbErrorSuccess && key != nullptr && (sel = adcrates->find_idx(key)) != -1)
                                {
                                    // all good
                                    adcrates->selected = sel;
                                    frate_changed = true;
                                }
                                else
                                {
                                    update_err("Could not get sensor bit depth", err);
                                }
                            }
                        }
                        ImGui::PopItemWidth();
                    }
                    // set binning
                    {
                        if (bin_changed)
                        {
                            VmbInt64_t bin;
                            err = allied_get_binning_factor(handle, &bin);
                            update_err("Binning changed", err);
                            sbin = bin;
                            bin_changed = false;
                        }
                        ImGui::Text("Image Bin:");
                        ImGui::SameLine();
                        ImGui::PushItemWidth(TEXT_BASE_WIDTH * 5);
                        if (ImGui::InputInt("##bin", &sbin, 0, 0, capturing ? ImGuiInputTextFlags_ReadOnly : 0))
                        {
                            if (sbin < 1)
                                sbin = 1;
                        }
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Update##Bin") && !capturing)
                        {
                            bin_changed = true;
                            size_changed = true;
                            ofst_changed = true;
                            err = allied_set_binning_factor(handle, sbin);
                            if (err != VmbErrorSuccess)
                            {
                                errmsg = string_format("Could not set binning to %d: ", sbin) + std::string(allied_strerr(err));
                            }
                        }
                    }
                    // set width + height
                    {
                        if (size_changed)
                        {
                            VmbInt64_t width, height;
                            err = allied_get_image_size(handle, &width, &height);
                            update_err("Size changed", err);
                            frate_changed = true;
                            swid = width;
                            shgt = height;
                            size_changed = false;
                        }
                        ImGui::Text("Image Size:");
                        ImGui::SameLine();
                        ImGui::PushItemWidth(TEXT_BASE_WIDTH * 5);
                        ImGui::InputInt("##width", &swid, 0, 0, capturing ? ImGuiInputTextFlags_ReadOnly : 0);
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        ImGui::Text(" x ");
                        ImGui::SameLine();
                        ImGui::PushItemWidth(TEXT_BASE_WIDTH * 5);
                        ImGui::InputInt("##height", &shgt, 0, 0, capturing ? ImGuiInputTextFlags_ReadOnly : 0);
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
                        ImGui::InputInt(("##ofstx" + info.idstr).c_str(), &ofx, 0, 0, 0);
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        ImGui::Text(" x ");
                        ImGui::SameLine();
                        ImGui::PushItemWidth(TEXT_BASE_WIDTH * 5);
                        ImGui::InputInt(("##ofsty" + info.idstr).c_str(), &ofy, 0, 0, 0);
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Update##Ofst"))
                        {
                            ofst_changed = true;
                            err = allied_set_image_ofst(handle, ofx, ofy);
                            if (err != VmbErrorSuccess)
                            {
                                errmsg = "Could not set image offset: " + std::string(allied_strerr(err));
                            }
                        }
                    }
                }

                // set exposure
                // ImGui::PushStyleColor(ImGuiCol_Text, header_col);
                // ImGui::TextSeparator((char *)"Exposure Properties");
                // ImGui::PopStyleColor();
                if (ImGui::CollapsingHeader("Exposure Properties"))
                {
                    {
                        if (exp_changed)
                        {
                            err = allied_get_exposure_range_us(handle, &expmin, &expmax, &expstep);
                            update_err("Get exposure range", err);
                            err = allied_get_exposure_us(handle, &currexp);
                            update_err("Get exposure", err);
                            frate_changed = true;
                            exp_changed = false;
                        }
                        ImGui::PushItemWidth(TEXT_BASE_WIDTH * 25);
                        if (ImGui::InputDouble("Exposure (us)", &currexp, expstep, ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            if (currexp < expmin)
                                currexp = expmin;
                            if (currexp > expmax)
                                currexp = expmax;
                        }
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Update##Exposure"))
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
                    // set framerate
                    {
                        bool old_frate_auto = frate_auto;
                        if (ImGui::Checkbox("Auto Frame Rate", &frate_auto))
                        {
                            err = allied_set_acq_framerate_auto(handle, frate_auto);
                            if (err != VmbErrorSuccess)
                            {
                                frate_auto = old_frate_auto;
                            }
                            update_err("Auto frame rate set", err);
                            err = allied_get_acq_framerate_auto(handle, &frate_auto);
                            if (err != VmbErrorSuccess)
                            {
                                frate_auto = old_frate_auto;
                            }
                            update_err("Auto frame rate get", err);
                            frate_changed = true;
                        }
                        ImGui::PushItemWidth(TEXT_BASE_WIDTH * 25);
                        if (ImGui::InputDouble("Frame Rate (Hz)", &frate, 0, 0, "%.4f", frate_auto ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            if (frate < frate_min)
                                frate = frate_min;
                            if (frate > frate_max)
                                frate = frate_max;
                        }
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Update##FrameRate") && !frate_auto)
                        {
                            if (frate < frate_min)
                                frate = frate_min;
                            if (frate > frate_max)
                                frate = frate_max;
                            err = allied_set_acq_framerate(handle, frate);
                            update_err("Set frame rate", err);
                            frate_changed = true;
                            stat.reset();
                        }
                    }
                    // select trigger line and source
                    if (triglines != nullptr && trigsrcs != nullptr)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, header_col);
                        ImGui::TextSeparator((char *)"Camera GPIO");
                        ImGui::PopStyleColor();
                        if (trigline_changed) // trig line changed, update source selection
                        {
                            int sel = trigsrcs->selected;
                            const char *key;
                            err = allied_get_trigline_src(handle, &key);
                            update_err("Could not get trigline source", err);
                            sel = trigsrcs->find_idx(key);
                            if (sel >= 0)
                                trigsrcs->selected = sel;
                            trigline_changed = false;
                        }
                        ImGui::Text("Trigger Line:");
                        ImGui::SameLine();
                        ImGui::PushItemWidth(TEXT_BASE_WIDTH * (triglines->maxlen + 6));
                        int sel = triglines->selected;
                        if (ImGui::Combo("##trigline", &sel, triglines->arr, triglines->narr) && !capturing)
                        {
                            err = allied_set_trigline(handle, triglines->arr[sel]);
                            update_err("Select trigger line", err);
                            if (err != VmbErrorSuccess)
                            {
                                goto trigline_clear;
                            }
                            const char *key = nullptr;
                            err = allied_get_trigline(handle, &key);
                            if (err == VmbErrorSuccess && key != nullptr && (sel = triglines->find_idx(key)) != -1)
                            {
                                // all good
                                triglines->selected = sel;
                                trigline_changed = true;
                                goto trigline_clear;
                            }
                            else
                            {
                                update_err("Could not get trigger line", err);
                            }
                        }
                        ImGui::SameLine();
                        ImGui::Text("     Source:");
                        ImGui::SameLine();
                        sel = trigsrcs->selected;
                        ImGui::PushItemWidth(TEXT_BASE_WIDTH * (trigsrcs->maxlen + 6));
                        if (ImGui::Combo("##trigsrc", &sel, trigsrcs->arr, trigsrcs->narr) && !capturing)
                        {
                            err = allied_set_trigline_src(handle, trigsrcs->arr[sel]);
                            update_err("Select trigger src", err);
                            const char *key = nullptr;
                            err = allied_get_trigline_src(handle, &key);
                            if (err == VmbErrorSuccess && key != nullptr && (sel = trigsrcs->find_idx(key)) != -1)
                            {
                                // all good
                                trigsrcs->selected = sel;
                            }
                            else
                            {
                                update_err("Could not get trigline src", err);
                            }
                        }
                        ImGui::PopItemWidth();
                    trigline_clear:
                        ImGui::PopItemWidth();
                    }
                }
                // Start/stop capture
                if (!capturing)
                {
                    if (pressed_stop)
                        pressed_stop = false;
                    if (ImGui::Button("Start Capture") && !pressed_start)
                    {
                        pressed_start = true;
                        err = start_capture();
                        if (err != VmbErrorSuccess)
                            pressed_start = false;
                    }
                }
                else
                {
                    if (pressed_start)
                        pressed_start = false;
                    if (ImGui::Button("Stop Capture") && !pressed_stop)
                    {
                        pressed_stop = true;
                        err = stop_capture();
                        if (err != VmbErrorSuccess)
                            pressed_stop = false;
                    }
                }
                ImGui::PushStyleColor(ImGuiCol_Text, header_col);
                ImGui::TextSeparator((char *)link_speed_str.c_str());
                ImGui::PopStyleColor();
                // set link speed
                {
                    if (speed == 0) // init
                        speed = throughput / 1000 / 1000;
                    bool update = false;
                    ImGui::Text("Link Speed (Current: %3lld MBps):", throughput / 1000 / 1000);
                    ImGui::SameLine();
                    ImGui::PushItemWidth(TEXT_BASE_WIDTH * 5);
                    if (ImGui::InputInt("##speed", &speed, 0, 0, capturing ? ImGuiInputTextFlags_ReadOnly : 0))
                    {
                        if (speed < throughput_min / 1000 / 1000)
                            speed = throughput_min / 1000 / 1000;
                        if (speed > throughput_max / 1000 / 1000)
                            speed = throughput_max / 1000 / 1000;
                    }
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Update##Speed") && !capturing)
                    {
                        update = true;
                    }
                    if (update)
                    {
                        err = allied_set_throughput_limit(handle, speed * 1000 * 1000);
                        update_err("Set link speed", err);
                        if (err == VmbErrorSuccess)
                        {
                            VmbInt64_t _throughput;
                            err = allied_get_throughput_limit(handle, &_throughput);
                            update_err("Get link speed", err);
                            if (err == VmbErrorSuccess)
                            {
                                throughput = _throughput;
                            }
                            frate_changed = true;
                        }
                        else
                        {
                            eprintlf("Error setting link speed to %d Bps: %s", speed * 1000 * 1000, allied_strerr(err));
                        }
                        speed = throughput / 1000 / 1000;
                    }
                }
                ImGui::PushStyleColor(ImGuiCol_Text, header_col);
                ImGui::TextSeparator((char *)"Statistics");
                ImGui::PopStyleColor();
                // Capture stats display
                double avg, std;
                stat.get_stats(avg, std);
                ImGui::Text(
                    "Frame Time: %.3f +/- %.6f ms", avg * 1e-3, std * 1e-3);
                ImGui::Text("Frame Rate: %.3f FPS | Expected max: %.3f FPS", 1e6 / avg, frate);
                ImGui::Separator();
                // Error message display
                ImGui::Text("Last error: %s", errmsg.c_str());
                if (ImGui::Button("Clear"))
                {
                    errmsg = "";
                }
                ImGui::TextSeparator((char *)"Image Display");
                // Image Display
                {
                    GLuint texture = 0;
                    uint32_t width = 0, height = 0;
                    if (show)
                    {
                        img.get_texture(texture, width, height);
                    }
                    ImGui::Text("ViewFinder | %u x %u | Collision: %u, Stall: %u", width, height, img.collision, img.stall);
                    if (show)
                    {
                        ImGui::Image((void *)(intptr_t)texture, render_size(width, height));
                    }
                }
            outside:
                assert(true);
            }
            ImGui::PopID();
            ImGui::End();
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

    bool running()
    {
        return capturing;
    }

    VmbError_t start_capture()
    {
        VmbError_t err = VmbErrorSuccess;
        if (handle != nullptr && !capturing)
        {
            stat.reset();
            img.collision = 0;
            img.stall = 0;
            err = allied_start_capture(handle); // set the callback here
            update_err("Start capture", err);
        }
        return err;
    }

    VmbError_t stop_capture()
    {
        VmbError_t err = VmbErrorSuccess;
        if (handle != nullptr && capturing)
        {
            err = allied_stop_capture(handle);
            update_err("Stop capture", err);
            if (adio_hdl != nullptr && adio_bit >= 0)
            {
                this->state = 0;
                WriteBit_aDIO(adio_hdl, 0, adio_bit, this->state);
            }
        }
        return err;
    }

    ~ImageDisplay()
    {
        close_camera();
    }

    static void Callback(const AlliedCameraHandle_t handle, const VmbHandle_t stream, VmbFrame_t *frame, void *user_data)
    {
        assert(user_data);
        ImageDisplay *self = (ImageDisplay *)user_data;
        if (self->adio_hdl != nullptr && self->adio_bit >= 0)
        {
            self->state = ~self->state;
            WriteBit_aDIO(self->adio_hdl, 0, self->adio_bit, self->state);
        }
        self->stat.update();
        self->img.update(frame);
    }
};
