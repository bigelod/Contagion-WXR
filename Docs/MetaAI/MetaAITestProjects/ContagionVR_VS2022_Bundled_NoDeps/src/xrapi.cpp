
#include "xrapi.h"
#include "math.h"
#include "gameui.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <sstream>
#include <cstdio>
#include <Windows.h>

#pragma comment(lib, "ws2_32.lib")

static std::thread g_recvThr;
static std::thread g_sendThr;
static std::atomic<bool> g_run{false};
static SOCKET g_sockIn = INVALID_SOCKET;
static SOCKET g_sockOut = INVALID_SOCKET;
static int g_bindPort = 7872;
static float g_hapticL=0, g_hapticR=0;
static std::mutex g_hapMtx;

static float ParseFloat(const std::vector<std::string>& parts, int idx){
    if(idx<0 || idx >= (int)parts.size()) return 0;
    try{ return std::stof(parts[idx]); }catch(...){ return 0; }
}

static std::vector<std::string> SplitSpaces(const std::string& s){
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while(iss >> tok) out.push_back(tok);
    return out;
}

static void EnsureVersionFile(){
    // Z:/tmp/xr/version = 0.4  (Z: is host root in Wine/Winlator)
    CreateDirectoryA("Z:\\tmp", nullptr);
    CreateDirectoryA("Z:\\tmp\\xr", nullptr);
    const char* path = "Z:\\tmp\\xr\\version";
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(h!=INVALID_HANDLE_VALUE){
        DWORD written; const char* txt="0.4"; WriteFile(h, txt, (DWORD)strlen(txt), &written, nullptr); CloseHandle(h);
    }
}

static void ParseXrString(const std::string& s){
    if(s.empty()) return;
    auto parts = SplitSpaces(s);
    if(parts.size() < 30) return;
    int idx=0;
    if(parts[0].rfind("client",0)==0) idx=1;
    try{
        XrFrame f{};
        // left quat
        Quat lQuat{ ParseFloat(parts, idx), ParseFloat(parts, idx+1), ParseFloat(parts, idx+2), ParseFloat(parts, idx+3)}; idx+=4;
        Vec2 lStick{ ParseFloat(parts, idx), ParseFloat(parts, idx+1)}; idx+=2;
        Vec3 lPos{ ParseFloat(parts, idx), ParseFloat(parts, idx+1), ParseFloat(parts, idx+2)}; idx+=3;
        Quat rQuat{ ParseFloat(parts, idx), ParseFloat(parts, idx+1), ParseFloat(parts, idx+2), ParseFloat(parts, idx+3)}; idx+=4;
        Vec2 rStick{ ParseFloat(parts, idx), ParseFloat(parts, idx+1)}; idx+=2;
        Vec3 rPos{ ParseFloat(parts, idx), ParseFloat(parts, idx+1), ParseFloat(parts, idx+2)}; idx+=3;
        Quat hQuat{ ParseFloat(parts, idx), ParseFloat(parts, idx+1), ParseFloat(parts, idx+2), ParseFloat(parts, idx+3)}; idx+=4;
        Vec3 hPos{ ParseFloat(parts, idx), ParseFloat(parts, idx+1), ParseFloat(parts, idx+2)}; idx+=3;
        float ipd = ParseFloat(parts, idx); idx++;
        float fovH = ParseFloat(parts, idx); idx++;
        float fovV = ParseFloat(parts, idx); idx++;
        int frameId=0; try{ frameId=std::stoi(parts[idx]); }catch(...){} idx++;
        std::string boolStr; std::string hmdType="META";
        if(idx < (int)parts.size()){ boolStr=parts[idx]; idx++; if(idx < (int)parts.size()) hmdType=parts[idx]; }

        f.left.pos = lPos; f.left.rot = lQuat; f.left.valid = true;
        f.right.pos = rPos; f.right.rot = rQuat; f.right.valid = true;
        f.hmd.pos = hPos; f.hmd.rot = hQuat; f.hmd.valid = true;
        f.leftStick = lStick; f.rightStick = rStick;
        f.ipd = ipd; f.fovH = fovH; f.fovV = fovV; f.frameId = frameId;
        strncpy_s(f.hmdType, hmdType.c_str(), sizeof(f.hmdType)-1);

        if(boolStr.length()>=19){
            f.lGrip = boolStr[0]=='T'; f.lMenu = boolStr[1]=='T'; f.lThumbPress = boolStr[2]=='T';
            f.lThumbLeft = boolStr[3]=='T'; f.lThumbRight = boolStr[4]=='T'; f.lThumbUp = boolStr[5]=='T'; f.lThumbDown = boolStr[6]=='T';
            f.lTrigger = boolStr[7]=='T'; f.lX = boolStr[8]=='T'; f.lY = boolStr[9]=='T';
            f.rA = boolStr[10]=='T'; f.rB = boolStr[11]=='T'; f.rGrip = boolStr[12]=='T'; f.rThumbPress = boolStr[13]=='T';
            f.rThumbLeft = boolStr[14]=='T'; f.rThumbRight = boolStr[15]=='T'; f.rThumbUp = boolStr[16]=='T'; f.rThumbDown = boolStr[17]=='T'; f.rTrigger = boolStr[18]=='T';
        }
        SetXrFrame(f);
    }catch(...){}
}

static void RecvLoop(){
    char buf[4096];
    sockaddr_in from{}; int fromLen=sizeof(from);
    while(g_run){
        int len = recvfrom(g_sockIn, buf, sizeof(buf)-1, 0, (sockaddr*)&from, &fromLen);
        if(len<=0){ Sleep(1); continue; }
        buf[len]=0;
        ParseXrString(std::string(buf));
    }
}
static void SendLoop(){
    sockaddr_in out{}; out.sin_family=AF_INET; out.sin_port=htons(7278);
    out.sin_addr.s_addr=inet_addr("127.0.0.1");
    while(g_run){
        float hl, hr;
        { std::lock_guard<std::mutex> l(g_hapMtx); hl=g_hapticL; hr=g_hapticR; }
        // format: left_haptic right_haptic vr_mode 3d_mode fovH fovV
        // vr_mode = 1 for VR gameplay, 0 for flat menu (from your earlier 0 0 1 0 90 90)
        int vrMode = GetDesiredVrMode(); // 0 pause -> flat, 1 -> VR
        // Use 104.5 104.5 as you example, but keep 90 90 fallback if no data yet
        XrFrame fr = GetXrFrameCopy();
        float fH = fr.hasData ? fr.fovH : 104.5f;
        float fV = fr.hasData ? fr.fovV : 104.5f;
        if(fH<10) fH=104.5f; if(fV<10) fV=104.5f;
        char msg[128];
        // keep 6 values: 0 0 0 0 104.5 104.5 example -> left right vr? Actually your doc: 0 0 0 0 104.5 104.5
        // We send haptics in first two, vrMode in third, 0 in fourth
        int n = sprintf_s(msg, "%.3f %.3f %d 0 %.2f %.2f", hl, hr, vrMode, fH, fV);
        sendto(g_sockOut, msg, n, 0, (sockaddr*)&out, sizeof(out));
        // Decay haptics
        { std::lock_guard<std::mutex> l(g_hapMtx); g_hapticL *= 0.9f; g_hapticR *= 0.9f; if(g_hapticL<0.01f) g_hapticL=0; if(g_hapticR<0.01f) g_hapticR=0; }
        Sleep(11); // ~90Hz to keep WinlatorXR active
    }
}

void Xr_Init(){
    WSADATA w; WSAStartup(MAKEWORD(2,2), &w);
    EnsureVersionFile();
    g_sockIn = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(7872); a.sin_addr.s_addr=htonl(INADDR_ANY);
    if(bind(g_sockIn, (sockaddr*)&a, sizeof(a))!=0){
        closesocket(g_sockIn);
        g_sockIn = socket(AF_INET, SOCK_DGRAM, 0);
        a.sin_port=htons(7873);
        if(bind(g_sockIn, (sockaddr*)&a, sizeof(a))!=0){
            // failed both
        }else g_bindPort=7873;
    }else g_bindPort=7872;
    g_sockOut = socket(AF_INET, SOCK_DGRAM, 0);
    g_run=true;
    g_recvThr = std::thread(RecvLoop);
    g_sendThr = std::thread(SendLoop);
}
void Xr_Shutdown(){
    g_run=false;
    if(g_sockIn!=INVALID_SOCKET) closesocket(g_sockIn);
    if(g_sockOut!=INVALID_SOCKET) closesocket(g_sockOut);
    if(g_recvThr.joinable()) g_recvThr.join();
    if(g_sendThr.joinable()) g_sendThr.join();
    WSACleanup();
}
void Xr_SetHaptics(float l, float r){
    std::lock_guard<std::mutex> l(g_hapMtx);
    g_hapticL = l; g_hapticR = r;
}
