/*
 * Copyright 2023 ZF Friedrichshafen AG
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifdef _WIN32
#include <Windows.h>
#else
 #include <sys/auxv.h>
#define _access(f, x) access(f, x)
#endif
#include <stdint.h>
#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <malloc.h>
#include <cmath>
#include <string>
#include "esminiLib.hpp"
extern "C" {
#define XILENV_INTERFACE_TYPE 1
#include "XilEnvExtProc.h"
#define XILENV_NO_MAIN
#include "XilEnvExtProcMain.c"
}

// WARNING - esmini must stay a pure visualizer, driven by the real
// motor/battery/transmission/vehicle model chain's Speed signal. This
// INTERNAL_CAR_MODEL block instead runs esmini's own built-in
// SE_SimpleVehicle dynamics directly from raw pedal input (AcceleratorPosition),
// completely bypassing the real powertrain model - two disagreeing sources
// of truth for the car's position. It exists only for developers who want to
// experiment with esmini's built-in vehicle dynamics in isolation from the
// real model. It must stay compiled out - or at minimum OpenScen_UseCarModel
// must stay false - in the demo everyone else runs. Do not flip
// OpenScen_UseCarModel's default to true "to make it drive" - that silently
// switches the whole demo onto the wrong source of truth for the car's
// position. See CyclicCall() below for how the real (default) path works.
#define INTERNAL_CAR_MODEL

#define UNUSED(x) (void)(x)

double abt_per;
double OpenScen_SteerAngle = 0.0;
double OpenScen_Throttle = 0.0;
double OpenScen_VehicleSpeed = 0.0;
double OpenScen_Pos_m = 0.0;
WSC_TYPE_INT32 OpenScen_StayOnRoad = 1;
WSC_TYPE_INT32 OpenScen_RooadId = 1;
WSC_TYPE_INT32 OpenScen_LaneId = -1;
double OpenScen_LaneOffset = 0.0;
double OpenScen_XPos_m = 0.0;
double OpenScen_YPos_m = 0.0;
double OpenScen_ZPos_m = 0.0;
double OpenScen_Yaw_rad = 0.0;
double OpenScen_Pitch_rad = 0.0;
double OpenScen_Roll_rad = 0.0;
double AcceleratorPosition = 0.0;
// Speed (m/s) computed by the real powertrain model chain (Battery ->
// Motor -> Transmission -> VehicleModel) on the XilEnv Blackboard. This -
// not esmini's own vehicle dynamics - is what actually moves the car; see
// CyclicCall().
double Speed = 0.0;
// Total length (m) of the current road, parsed once at startup from the
// referenced OpenDRIVE file (see ResolveRoadLength()) so OpenScen_Pos_m can
// wrap explicitly instead of relying on esmini's implicit handling. 0 means
// "could not be determined" - wrapping is then skipped rather than guessed.
double g_RoadLength = 0.0;

// esmini's own built-in vehicle dynamics (see INTERNAL_CAR_MODEL warning
// above). Off by default - the real powertrain model chain drives the car.
bool OpenScen_UseCarModel = false;
bool OpenScen_UseCarModelLast = false;

void reference_varis(void)
{
    REF_DIR_DOUBLE_VAR(READ_ONLY_REFERENCE, abt_per, "XilEnv.SampleTime", "");
#ifdef INTERNAL_CAR_MODEL
    REF_DIR_ENUM_VAR_AI(READ_WRITE_REFERENCE, OpenScen_UseCarModel, "", CONVTYPE_TEXTREPLACE, "0 0 \"No Car Model\";1 1 \"Car Model\";", 0, 1, 0, 0, COLOR_UNDEFINED, 0, 1);
    REF_DIR_DOUBLE_VAR(READ_ONLY_REFERENCE, OpenScen_SteerAngle, "OpenScen_SteerAngle", "");
    REF_DIR_DOUBLE_VAR(READ_WRITE_REFERENCE, OpenScen_Throttle, "OpenScen_Throttle", "");
    REF_DIR_DOUBLE_VAR(READ_ONLY_REFERENCE, AcceleratorPosition, "AcceleratorPosition", "");
#endif
    REF_DIR_DWORD_VAR_AI(READ_ONLY_REFERENCE, OpenScen_StayOnRoad, "", CONVTYPE_TEXTREPLACE, "0 0 \"free\";1 1 \"stay on road\"; 2 2 \"free 6 DOF\";", 0, 1, 0, 0, COLOR_UNDEFINED, 0, 1);
    REF_DIR_DWORD_VAR(READ_WRITE_REFERENCE, OpenScen_RooadId, "OpenScen_RooadId", "");
    REF_DIR_DWORD_VAR(READ_ONLY_REFERENCE, OpenScen_LaneId, "OpenScen_LaneId", "");
    REF_DIR_DOUBLE_VAR(READ_ONLY_REFERENCE, OpenScen_LaneOffset, "OpenScen_LaneOffset", "");
    REF_DIR_DOUBLE_VAR(READ_WRITE_REFERENCE, OpenScen_Pos_m, "OpenScen_Pos_m", "");
    REF_DIR_DOUBLE_VAR(READ_ONLY_REFERENCE, Speed, "Speed", "");
    REF_DIR_DOUBLE_VAR(READ_WRITE_REFERENCE, OpenScen_XPos_m, "OpenScen_XPos_m", "");
    REF_DIR_DOUBLE_VAR(READ_WRITE_REFERENCE, OpenScen_YPos_m, "OpenScen_YPos_m", "");
    REF_DIR_DOUBLE_VAR(READ_WRITE_REFERENCE, OpenScen_ZPos_m, "OpenScen_ZPos_m", "");
    REF_DIR_DOUBLE_VAR(READ_WRITE_REFERENCE, OpenScen_Yaw_rad, "OpenScen_Yaw_rad", "");
    REF_DIR_DOUBLE_VAR(READ_WRITE_REFERENCE, OpenScen_Pitch_rad, "OpenScen_Pitch_rad", "");
    REF_DIR_DOUBLE_VAR(READ_WRITE_REFERENCE, OpenScen_Roll_rad, "OpenScen_Roll_rad", "");
    REF_DIR_DOUBLE_VAR(READ_WRITE_REFERENCE, OpenScen_VehicleSpeed, "OpenScen_VehicleSpeed", "");
}

int CyclicCall(EXTERN_PROCESS_TASK_HANDLE, double Period)
{
    float dt = Period;

    static void *vehicleHandle = NULL;

#ifdef INTERNAL_CAR_MODEL
    // Experimental only - see the INTERNAL_CAR_MODEL warning above. This
    // whole block only runs anything meaningful when OpenScen_UseCarModel is
    // explicitly set to true, which is not the default.
    if (OpenScen_UseCarModel != OpenScen_UseCarModelLast) {
        if (vehicleHandle == NULL) {
            SE_ScenarioObjectState objectState;
            // Initialize the vehicle model, fetch initial state from the scenario
            SE_GetObjectState(0, &objectState);
            vehicleHandle = SE_SimpleVehicleCreate(objectState.x, objectState.y, objectState.h, 4.0, 0.0);
        }
        SE_ViewerShowFeature(4 + 8, OpenScen_UseCarModel);  // NODE_MASK_TRAIL_DOTS (1 << 2) & NODE_MASK_ODR_FEATURES (1 << 3),
        OpenScen_UseCarModelLast = OpenScen_UseCarModel;
    }

    if (OpenScen_UseCarModel && (vehicleHandle != NULL)) {
        // Step vehicle model with driver input, but wait until time > 0
        if (SE_GetSimulationTime() > 0) {
            // Drive via the potentiometer: AcceleratorPosition (0..100 %,
            // set by ExtProc_Arduino from the pot) maps directly to throttle.
            OpenScen_Throttle = AcceleratorPosition / 100.0;
            SE_SimpleVehicleControlAnalog(vehicleHandle, dt, OpenScen_Throttle, -OpenScen_SteerAngle);
        }

        // Fetch updated state and report to scenario engine
        SE_SimpleVehicleState vehicleState = { 0, 0, 0, 0, 0, 0, 0, 0};
        SE_SimpleVehicleGetState(vehicleHandle, &vehicleState);
        OpenScen_XPos_m = vehicleState.x;
        OpenScen_YPos_m = vehicleState.y;
        OpenScen_ZPos_m = vehicleState.z;
        OpenScen_Yaw_rad = vehicleState.h;
        OpenScen_Pitch_rad = vehicleState.p;
        OpenScen_VehicleSpeed = vehicleState.speed;
    }
#endif
    if (!OpenScen_UseCarModel) {
        // Default (real) path: esmini is a pure visualizer. Integrate the
        // real powertrain model's Speed along the road's s-coordinate
        // ourselves, wrapping explicitly with fmod (rather than relying on
        // esmini's implicit wrap-around) so behavior stays legible and
        // correct on arbitrarily long runs.
        OpenScen_Pos_m += (Speed / 3.6) * dt;  // Speed is km/h, convert to m/s
        if (g_RoadLength > 0.0) {
            OpenScen_Pos_m = std::fmod(OpenScen_Pos_m, g_RoadLength);
            if (OpenScen_Pos_m < 0.0) {
                OpenScen_Pos_m += g_RoadLength;
            }
        }
        OpenScen_VehicleSpeed = Speed / 3.6;
    }
    switch (OpenScen_StayOnRoad) {
    default:
    case 0:  // free
        // Report updated vehicle position and heading. z, pitch and roll will be aligned to the road
        SE_ReportObjectPosXYH(0, OpenScen_XPos_m, OpenScen_YPos_m, OpenScen_Yaw_rad);
        break;
    case 1: // stay on road
        SE_ReportObjectRoadPos(0, OpenScen_RooadId, OpenScen_LaneId, OpenScen_LaneOffset, OpenScen_Pos_m);
        break;
    case 2: // free 6 DOF
        SE_ReportObjectPos(0, OpenScen_XPos_m, OpenScen_YPos_m, OpenScen_ZPos_m, OpenScen_Yaw_rad, OpenScen_Pitch_rad, OpenScen_Roll_rad);
        break;
    }

    // Finally, update scenario using same time step as for vehicle model
    SE_StepDT(dt);
    return 0;
}

#ifdef _WIN32
BOOL GetOpenFilename(const char *szExtension, char *szFullPath, HWND hwnd)
{
    OPENFILENAME  ofn = { 0 };

    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = szExtension;
    ofn.nFilterIndex = 0;
    ofn.lpstrFile = szFullPath;
    ofn.nMaxFile = 255;
    ofn.Flags = OFN_CREATEPROMPT;

    return GetOpenFileName(&ofn);
}
#else
int GetModuleFileName(void *hModule, char *lpFilename, int nSize)
{
    UNUSED(hModule);
    const char *Path = (char*)getauxval(AT_EXECFN);
    if (Path == NULL) Path = "unknown";
    else Path = realpath(Path, NULL);
    if (Path == NULL) Path = "unknown";

    if (((int)strlen(Path) + 1) >= nSize) {
        strncpy(lpFilename, Path, nSize);
        lpFilename[nSize - 1] = 0;
    } else strcpy(lpFilename, Path);
    return (int)strlen(lpFilename);
}
#endif

static std::string ReadWholeFile(const char *path)
{
    std::string content;
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return content;
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        content.append(buf, n);
    }
    fclose(f);
    return content;
}

// Finds the first attr="..." after the first occurrence of tagHint (or
// anywhere in xml if tagHint is empty) and returns its value, or an empty
// string if not found. A tiny helper for the two narrow lookups below -
// not a general XML parser.
static std::string ExtractAttribute(const std::string &xml, const std::string &tagHint, const std::string &attr)
{
    size_t tagPos = tagHint.empty() ? 0 : xml.find(tagHint);
    if (tagPos == std::string::npos) {
        return std::string();
    }
    std::string needle = attr + "=\"";
    size_t attrPos = xml.find(needle, tagPos);
    if (attrPos == std::string::npos) {
        return std::string();
    }
    attrPos += needle.size();
    size_t endPos = xml.find('"', attrPos);
    if (endPos == std::string::npos) {
        return std::string();
    }
    return xml.substr(attrPos, endPos - attrPos);
}

// esminiLib has no API to query the currently loaded road's total length
// (checked esminiLib.hpp), so it's read directly out of the OpenDRIVE file:
// the .xosc's <LogicFile filepath="..."/> gives the .xodr path (resolved
// relative to the .xosc's own directory), and that file's top-level
// <road ... length="..."> attribute gives the length. Returns false (and
// leaves *out_length untouched) if it can't be determined - callers should
// treat that as "unknown", not fall back to a guessed number.
static bool ResolveRoadLength(const char *xoscPath, double *out_length)
{
    std::string xoscContent = ReadWholeFile(xoscPath);
    if (xoscContent.empty()) {
        return false;
    }

    std::string filepath = ExtractAttribute(xoscContent, "<LogicFile", "filepath");
    if (filepath.empty()) {
        return false;
    }

    std::string xoscDir;
    {
        std::string p = xoscPath;
        size_t slash = p.find_last_of("/\\");
        if (slash != std::string::npos) {
            xoscDir = p.substr(0, slash + 1);
        }
    }
    std::string xodrPath = xoscDir + filepath;

    std::string xodrContent = ReadWholeFile(xodrPath.c_str());
    if (xodrContent.empty()) {
        // The literal relative path (e.g. "../xodr/track.xodr") doesn't
        // necessarily match how the .xodr was actually laid out next to the
        // installed .xosc (esmini itself falls back to searching several
        // candidate paths for exactly this reason - see its own "search
        // paths attempted" log output). Try just the basename alongside the
        // .xosc before giving up.
        size_t slash = filepath.find_last_of("/\\");
        std::string basename = (slash != std::string::npos) ? filepath.substr(slash + 1) : filepath;
        xodrContent = ReadWholeFile((xoscDir + basename).c_str());
        if (xodrContent.empty()) {
            return false;
        }
    }

    std::string lengthStr = ExtractAttribute(xodrContent, "<road ", "length");
    if (lengthStr.empty()) {
        return false;
    }

    try {
        double length = std::stod(lengthStr);
        if (length <= 0.0) {
            return false;
        }
        *out_length = length;
        return true;
    } catch (...) {
        return false;
    }
}

int main(int argc, char* argv[])
{
    UNUSED(argc);
    UNUSED(argv);
    EXTERN_PROCESS_TASK_HANDLE Process;
#ifdef _WIN32
    HINSTANCE h_instance = GetModuleHandle(NULL);
#else
    void *h_instance = NULL;
#endif
    char Name[MAX_PATH];
    char OpenScenarioDescriptionFile[MAX_PATH+16];

    GetModuleFileName(h_instance, Name, sizeof(Name));
#ifdef _WIN32
    strupr(Name);
#endif
    SetExternalProcessExecutableName(Name);

    Process = BuildNewProcessAndConnectTo(Name, "", "", NULL, 100);
    if (Process == NULL) {
        SE_LogMessage("cannot connect to softcar\n");
        return 1;
    }
    if (GetEnvironmentVariableA("OPENSCENARIO_DESCRIPTION_FILE", OpenScenarioDescriptionFile, sizeof(OpenScenarioDescriptionFile)) <= 0) {
        // use default file inside the working directory
        if (_access("default.xosc", 4) != -1) {
            strcpy(OpenScenarioDescriptionFile, "default.xosc");
        } else {
            // or where the executable lives
            strcpy(OpenScenarioDescriptionFile, Name);
            char *p = OpenScenarioDescriptionFile + strlen(OpenScenarioDescriptionFile);
            while ((p > OpenScenarioDescriptionFile) && (*p != '\\') && (*p != '/')) {
                p--;
            }
            if ((*p == '\\') || (*p == '/')) p++;
            strcpy(p, "default.xosc");
        }
    }
#ifdef _WIN32
    else if (!strcmpi("select", OpenScenarioDescriptionFile)) {
        strcpy(OpenScenarioDescriptionFile, "");
        GetOpenFilename("INI-Dateien (*.xosc)\0*.xosc\0Alle Dateien (*.*)\0*.*\0\0", OpenScenarioDescriptionFile, NULL);
    }
#endif
    if (SE_Init(OpenScenarioDescriptionFile , 0, 1, 1, 0) != 0)
	{
		SE_LogMessage("Failed to initialize the scenario, quit\n");
        DisconnectFrom(Process, 1);
        return 1;
	}

	// Lock object to the original lane
	// If setting to false, the object road position will snap to closest lane
	SE_SetLockOnLane(0, true);

    SE_ViewerShowFeature(4 + 8, false);  // NODE_MASK_TRAIL_DOTS (1 << 2) & NODE_MASK_ODR_FEATURES (1 << 3)

    if (!ResolveRoadLength(OpenScenarioDescriptionFile, &g_RoadLength)) {
        SE_LogMessage("Warning: could not determine road length from the OpenDRIVE file; OpenScen_Pos_m will not wrap\n");
        g_RoadLength = 0.0;
    }

    while (1) {
        switch (WaitUntilFunctionCallRequest(Process)) {
        case 1: //reference:
            reference_varis();
            break;
        case 2: //init:
           // init_test_object();
            break;
        case 3: //cyclic:
            if (CyclicCall(Process, abt_per)) {
                return 1;
            }
            break;
        case 4: //terminate:
            //terminate_test_object();
            break;
        default:
        case 5:   // Process would be killed by Softcar -> no more calling, leave the loop!
            return 0;
        }
        AcknowledgmentOfFunctionCallRequest(Process, 0);
    }
	return 0;
}
