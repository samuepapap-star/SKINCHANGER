#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <sstream>
#include <wrl/client.h>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_set>
#include <d3d11.h>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <cmath>
#include <cctype>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <nlohmann/json.hpp>
#pragma comment(lib,"d3d11.lib")
using json=nlohmann::json;
struct Skin {std::string weapon,name,collection,variant; int level=1; bool favorite=false; std::string uuid,image; std::vector<std::string> variants; int maxLevel=1; bool applied=false;};
static std::string FetchCatalog(){std::string out; HINTERNET session=WinHttpOpen(L"VSkinPremium/2.0",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);if(!session)return out; HINTERNET connect=WinHttpConnect(session,L"valorant-api.com",INTERNET_DEFAULT_HTTPS_PORT,0);if(connect){HINTERNET request=WinHttpOpenRequest(connect,L"GET",L"/v1/weapons?language=en-US",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);if(request){DWORD timeout=10000;WinHttpSetTimeouts(request,timeout,timeout,timeout,timeout);if(WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,nullptr)){DWORD status=0,len=sizeof(status);WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&len,WINHTTP_NO_HEADER_INDEX);if(status==200){char buffer[8192];DWORD read=0;while(WinHttpReadData(request,buffer,sizeof(buffer),&read)&&read){if(out.size()+read>12*1024*1024){out.clear();break;}out.append(buffer,read);}}}WinHttpCloseHandle(request);}WinHttpCloseHandle(connect);}WinHttpCloseHandle(session);return out;}
static std::vector<Skin> ParseCatalog(const std::string& raw){std::vector<Skin> result;try{auto root=json::parse(raw);if(!root.contains("data")||!root["data"].is_array())return result;for(const auto& w:root["data"]){if(!w.is_object())continue;std::string weapon=w.value("displayName",std::string());if(weapon=="Melee")weapon="Melee";if(!w.contains("skins")||!w["skins"].is_array())continue;for(const auto& item:w["skins"]){if(!item.is_object())continue;Skin skin;skin.weapon=weapon;skin.name=item.value("displayName",std::string());skin.uuid=item.value("uuid",std::string());skin.image=item.value("displayIcon",std::string());skin.collection=skin.name;skin.variant="Default";if(item.contains("chromas")&&item["chromas"].is_array()){for(const auto& chroma:item["chromas"]){if(chroma.is_object())skin.variants.push_back(chroma.value("displayName",std::string("Default")));}}if(skin.variants.empty())skin.variants.push_back("Default");if(item.contains("levels")&&item["levels"].is_array())skin.maxLevel=std::max(1,(int)item["levels"].size());if(!skin.uuid.empty()&&!skin.name.empty()){
 auto pos=skin.name.rfind(' ' + weapon);
 if(pos!=std::string::npos && pos>0) skin.collection=skin.name.substr(0,pos);
 else if(skin.name=="Standard " + weapon || skin.name=="Random Favorite Skin") skin.collection="Standard";
 result.push_back(std::move(skin));}}}}catch(...){result.clear();}return result;}
// Official artwork is downloaded on demand from the public catalog; never touches the game.
static std::vector<unsigned char> FetchImage(const std::string& url){
 std::vector<unsigned char> bytes; const std::string prefix="https://media.valorant-api.com/";
 if(url.rfind(prefix,0)!=0)return bytes; // strict host allowlist
 std::string path=url.substr(std::string("https://media.valorant-api.com").size());
 if(path.find("..")!=std::string::npos || path.find('?')!=std::string::npos)return bytes;
 std::wstring wpath(path.begin(),path.end());
 HINTERNET h=WinHttpOpen(L"VSkinPremium/3.0",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
 if(!h)return bytes;HINTERNET c=WinHttpConnect(h,L"media.valorant-api.com",INTERNET_DEFAULT_HTTPS_PORT,0);
 if(c){HINTERNET r=WinHttpOpenRequest(c,L"GET",wpath.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
 if(r){WinHttpSetTimeouts(r,6000,6000,6000,6000);
 if(WinHttpSendRequest(r,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(r,nullptr)){
 DWORD status=0,len=sizeof(status);WinHttpQueryHeaders(r,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&len,WINHTTP_NO_HEADER_INDEX);
 if(status==200){unsigned char b[8192];DWORD n=0;while(WinHttpReadData(r,b,sizeof(b),&n)&&n){if(bytes.size()+n>4*1024*1024){bytes.clear();break;}bytes.insert(bytes.end(),b,b+n);}}}
 WinHttpCloseHandle(r);}WinHttpCloseHandle(c);}WinHttpCloseHandle(h);return bytes;
}
static std::unordered_map<std::string,ID3D11ShaderResourceView*> textures;
static ID3D11Device* device=nullptr; static ID3D11DeviceContext* context=nullptr; static IDXGISwapChain* swapchain=nullptr; static ID3D11RenderTargetView* target=nullptr;
static ID3D11ShaderResourceView* DecodeTexture(const std::vector<unsigned char>& data){
 if(data.empty()||!device)return nullptr;
 Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
 if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return nullptr;
 Microsoft::WRL::ComPtr<IWICStream> stream;
 if(FAILED(factory->CreateStream(&stream))||FAILED(stream->InitializeFromMemory((BYTE*)data.data(),(DWORD)data.size())))return nullptr;
 Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
 if(FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder)))return nullptr;
 Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
 if(FAILED(decoder->GetFrame(0,&frame)))return nullptr;
 UINT w=0,h=0;frame->GetSize(&w,&h);if(!w||!h||w>4096||h>4096)return nullptr;
 Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
 if(FAILED(factory->CreateFormatConverter(&converter))||FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return nullptr;
 std::vector<BYTE> pixels((size_t)w*h*4);
 if(FAILED(converter->CopyPixels(nullptr,w*4,(UINT)pixels.size(),pixels.data())))return nullptr;
 D3D11_TEXTURE2D_DESC desc={};desc.Width=w;desc.Height=h;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 D3D11_SUBRESOURCE_DATA init={pixels.data(),w*4,0};Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
 if(FAILED(device->CreateTexture2D(&desc,&init,&tex)))return nullptr;
 ID3D11ShaderResourceView* view=nullptr;if(FAILED(device->CreateShaderResourceView(tex.Get(),nullptr,&view)))return nullptr;return view;
}
static void CreateTarget(){ID3D11Texture2D* back=nullptr; if(SUCCEEDED(swapchain->GetBuffer(0,IID_PPV_ARGS(&back)))){device->CreateRenderTargetView(back,nullptr,&target);back->Release();}}
static void CleanupTarget(){if(target){target->Release();target=nullptr;}}
static bool InitD3D(HWND hwnd){DXGI_SWAP_CHAIN_DESC sd={};sd.BufferCount=2;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.OutputWindow=hwnd;sd.SampleDesc.Count=1;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;D3D_FEATURE_LEVEL fl;auto hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swapchain,&device,&fl,&context);if(FAILED(hr))return false;CreateTarget();return true;}
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
static LRESULT WINAPI WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){if(ImGui_ImplWin32_WndProcHandler(hwnd,msg,wp,lp))return true;switch(msg){case WM_SIZE:if(device && wp!=SIZE_MINIMIZED){CleanupTarget();swapchain->ResizeBuffers(0,LOWORD(lp),HIWORD(lp),DXGI_FORMAT_UNKNOWN,0);CreateTarget();}return 0;case WM_SYSCOMMAND:if((wp&0xfff0)==SC_KEYMENU)return 0;break;case WM_DESTROY:PostQuitMessage(0);return 0;}return DefWindowProc(hwnd,msg,wp,lp);}
static std::filesystem::path ConfigPath(){char p[MAX_PATH]={};DWORD n=GetEnvironmentVariableA("APPDATA",p,MAX_PATH);auto dir=(n&&n<MAX_PATH)?std::filesystem::path(p)/"VSkinPremium":std::filesystem::current_path();std::error_code ec;std::filesystem::create_directories(dir,ec);return dir/"config.json";}
struct App {
 std::vector<Skin> skins;std::vector<std::string> logs;std::string page="Dashboard";std::string selectedUuid;char search[128]={};int weaponFilter=0;int selected=0;bool vsync=true;float scale=1.0f;float transition=0;std::string toast;float toastTimer=0;std::string imageUuid;bool imageAttempted=false;
 const std::vector<std::string> weapons={"All","Vandal","Phantom","Operator","Sheriff","Ghost","Classic","Spectre","Guardian","Bulldog","Melee","Ares","Odin","Marshal","Outlaw","Bucky","Judge","Frenzy","Stinger","Shorty"};
 App(){const std::vector<std::pair<std::string,std::string>> names={{"Vandal","Prime"},{"Vandal","Reaver"},{"Vandal","RGX 11z Pro"},{"Phantom","Oni"},{"Phantom","Spectrum"},{"Operator","Elderflame"},{"Sheriff","Ion"},{"Ghost","Sovereign"},{"Classic","Glitchpop"},{"Spectre","Protocol 781-A"},{"Guardian","Ruination"},{"Bulldog","Araxys"},{"Melee","Champions Blade"}};for(auto& [w,n]:names)skins.push_back({w,n,n,"Default",1,false});RefreshCatalog(false);Load();Log("Application initialized");}
 void RefreshCatalog(bool network){auto path=ConfigPath().parent_path()/"catalog-cache.json";std::string raw;if(network){raw=FetchCatalog();if(!raw.empty()){std::ofstream file(path,std::ios::binary);file<<raw;}}else{std::ifstream file(path,std::ios::binary);if(file)raw.assign(std::istreambuf_iterator<char>(file),{});}auto parsed=ParseCatalog(raw);if(!parsed.empty()){imageUuid.clear();imageAttempted=false;std::unordered_set<std::string> favorite,applied;for(auto& old:skins){if(old.favorite)favorite.insert(old.uuid);if(old.applied)applied.insert(old.uuid);}skins=std::move(parsed);for(auto& skin:skins){skin.favorite=favorite.contains(skin.uuid);skin.applied=applied.contains(skin.uuid);}selected=0;
 if(!selectedUuid.empty())for(int i=0;i<(int)skins.size();++i)if(skins[i].uuid==selectedUuid){selected=i;break;}
 Log("Catalog loaded: "+std::to_string(skins.size())+" skins");}else Log("Using demo catalog; choose Refresh Catalog to download all public skins");}
 void Log(std::string s){logs.push_back(std::move(s));if(logs.size()>150)logs.erase(logs.begin());}
 void Notify(std::string s){toast=s;toastTimer=3;Log(s);}
 void Load(){std::ifstream f(ConfigPath());if(!f)return;try{auto j=json::parse(f);vsync=j.value("vsync",true);scale=j.value("scale",1.f);selected=std::clamp(j.value("selected",0),0,std::max(0,(int)skins.size()-1));selectedUuid=j.value("selectedUuid",std::string());
 if(!selectedUuid.empty())for(int i=0;i<(int)skins.size();++i)if(skins[i].uuid==selectedUuid){selected=i;break;}if(j.contains("skins")&&j["skins"].is_array()){for(auto& entry:j["skins"]){if(!entry.is_object())continue;for(auto& s:skins)if(((!s.uuid.empty()&&s.uuid==entry.value("uuid",std::string()))||(s.name==entry.value("name",std::string())&&s.weapon==entry.value("weapon",std::string())))){s.favorite=entry.value("favorite",false);s.applied=entry.value("applied",false);s.variant=entry.value("variant",std::string("Default"));s.level=std::clamp(entry.value("level",1),1,s.maxLevel);}}}}catch(...){Log("Invalid configuration; defaults loaded");}}
 void Save(){json j={{"vsync",vsync},{"scale",scale},{"selected",selected},{"selectedUuid",skins.empty()?std::string():skins[std::clamp(selected,0,(int)skins.size()-1)].uuid},{"skins",json::array()}};for(auto& s:skins)j["skins"].push_back({{"uuid",s.uuid},{"name",s.name},{"weapon",s.weapon},{"favorite",s.favorite},{"applied",s.applied},{"variant",s.variant},{"level",s.level}});std::ofstream(ConfigPath())<<j.dump(2);}
 void Theme(){auto& st=ImGui::GetStyle();st.WindowRounding=12;st.ChildRounding=10;st.FrameRounding=7;st.PopupRounding=8;st.GrabRounding=6;st.WindowPadding=ImVec2(16,16);st.FramePadding=ImVec2(10,8);st.ItemSpacing=ImVec2(10,10);auto& c=st.Colors;c[ImGuiCol_WindowBg]=ImVec4(.051f,.059f,.078f,1);c[ImGuiCol_ChildBg]=ImVec4(.09f,.102f,.13f,1);c[ImGuiCol_FrameBg]=ImVec4(.13f,.145f,.18f,1);c[ImGuiCol_Button]=ImVec4(.18f,.19f,.23f,1);c[ImGuiCol_ButtonHovered]=ImVec4(1,.275f,.333f,.8f);c[ImGuiCol_ButtonActive]=ImVec4(1,.275f,.333f,1);c[ImGuiCol_Header]=ImVec4(.25f,.14f,.18f,1);c[ImGuiCol_HeaderHovered]=ImVec4(.65f,.22f,.28f,1);c[ImGuiCol_HeaderActive]=ImVec4(1,.275f,.333f,1);c[ImGuiCol_CheckMark]=ImVec4(1,.275f,.333f,1);c[ImGuiCol_SliderGrab]=ImVec4(1,.275f,.333f,1);c[ImGuiCol_Text]=ImVec4(.95f,.96f,.98f,1);}
 void Card(const char* label,const std::string& value){ImGui::BeginChild(label,ImVec2(0,94),ImGuiChildFlags_Borders);ImGui::TextDisabled("%s",label);ImGui::SetWindowFontScale(1.45f);ImGui::TextWrapped("%s",value.c_str());ImGui::SetWindowFontScale(1);ImGui::EndChild();}
 void LoadPreviewImage(){
 if(skins.empty())return;auto& skin=skins[selected];
 if(imageUuid==skin.uuid&&imageAttempted)return;
 imageUuid=skin.uuid;imageAttempted=true;
 if(skin.image.empty()||skin.uuid.empty()||textures.contains(skin.uuid))return;
 auto bytes=FetchImage(skin.image);auto* view=DecodeTexture(bytes);
 if(view){if(textures.size()>=32){for(auto& item:textures)if(item.second)item.second->Release();textures.clear();}textures[skin.uuid]=view;}
 }
 void Preview(){if(skins.empty()){ImGui::TextDisabled("No skins available");return;}auto& s=skins[selected];ImGui::BeginChild("Preview",ImVec2(0,245),ImGuiChildFlags_Borders);ImVec2 pos=ImGui::GetCursorScreenPos();ImVec2 avail=ImGui::GetContentRegionAvail();auto* dl=ImGui::GetWindowDrawList();ImVec2 a(pos.x+avail.x*.12f,pos.y+100),b(pos.x+avail.x*.88f,pos.y+155);dl->AddLine(a,b,IM_COL32(255,70,85,230),16);dl->AddLine(ImVec2(a.x+25,a.y-19),ImVec2(b.x-30,b.y-19),IM_COL32(220,224,233,220),5);dl->AddCircleFilled(ImVec2(b.x,b.y),14,IM_COL32(255,70,85,255));dl->AddText(ImVec2(pos.x+15,pos.y+14),IM_COL32(255,255,255,255),(s.weapon+" / "+s.name).c_str());dl->AddText(ImVec2(pos.x+15,pos.y+38),IM_COL32(180,185,195,255),("Variant: "+s.variant+" | Level: "+std::to_string(s.level)).c_str());if(!s.uuid.empty() && !s.image.empty()){
 auto it=textures.find(s.uuid);
 if(it!=textures.end()){
 ImGui::SetCursorScreenPos(ImVec2(pos.x+20,pos.y+58));
 ImGui::Image((ImTextureID)it->second,ImVec2(std::max(120.f,avail.x-40),135));
 }else{ImGui::Dummy(ImVec2(1,165));ImGui::TextDisabled("Artwork not loaded - click Load artwork");}
 }else{ImGui::Dummy(ImVec2(1,165));ImGui::TextDisabled("No official artwork in demo catalog");}
 ImGui::TextDisabled("Public artwork preview only; does not alter the game");ImGui::EndChild();}
 void Catalog(bool onlyFav=false){ImGui::InputTextWithHint("##search","Search skins...",search,sizeof(search));ImGui::SameLine();ImGui::SetNextItemWidth(165);if(ImGui::BeginCombo("##weapon",weapons[weaponFilter].c_str())){for(int i=0;i<(int)weapons.size();++i)if(ImGui::Selectable(weapons[i].c_str(),weaponFilter==i))weaponFilter=i;ImGui::EndCombo();}ImGui::Separator();ImGui::BeginChild("catalog",ImVec2(0,0));int count=0;for(int i=0;i<(int)skins.size();++i){auto& s=skins[i];if(onlyFav&&!s.favorite)continue;if(weaponFilter&&s.weapon!=weapons[weaponFilter])continue;std::string hay=s.name+" "+s.weapon+" "+s.collection;std::string needle=search;std::transform(hay.begin(),hay.end(),hay.begin(),[](unsigned char c){return (char)std::tolower(c);});std::transform(needle.begin(),needle.end(),needle.begin(),[](unsigned char c){return (char)std::tolower(c);});if(hay.find(needle)==std::string::npos)continue;ImGui::PushID(i);ImGui::BeginChild("tile",ImVec2(0,110),ImGuiChildFlags_Borders);ImGui::TextColored(ImVec4(1,.275f,.333f,1),"%s",s.weapon.c_str());ImGui::SameLine();ImGui::Text("%s",s.name.c_str());ImGui::TextDisabled("Collection: %s | Variant: %s | Level %d",s.collection.c_str(),s.variant.c_str(),s.level);if(ImGui::Button(selected==i?"Selected":"Select")){selected=i;selectedUuid=s.uuid;Notify("Selected "+s.name);Save();}ImGui::SameLine();if(ImGui::Button(s.favorite?"Unfavorite":"Favorite")){s.favorite=!s.favorite;Save();}ImGui::EndChild();ImGui::PopID();++count;}if(!count)ImGui::TextDisabled("No skins match the filters.");ImGui::EndChild();}
 void Dashboard(){ImGui::Text("WELCOME TO VSKIN PREMIUM");ImGui::TextDisabled("Standalone cosmetic catalog and collection manager");if(ImGui::Button("Sync complete skin catalog")){RefreshCatalog(true);Notify("Catalog currently contains "+std::to_string(skins.size())+" skins (check logs for cache/failure)");}ImGui::Spacing();int fav=0;for(auto& s:skins)fav+=s.favorite;float w=ImGui::GetContentRegionAvail().x;ImGui::Columns(2,nullptr,false);Card("CATALOG",std::to_string(skins.size())+" skins");Card("SELECTED",skins.empty()?"None":skins[selected].weapon);ImGui::NextColumn();Card("FAVORITES",std::to_string(fav));Card("RENDERER","DirectX 11");ImGui::Columns(1);ImGui::Separator();ImGui::Text("Skin distribution");for(size_t i=1;i<weapons.size();++i){int n=0;for(auto& s:skins)n+=s.weapon==weapons[i];ImGui::Text("%s",weapons[i].c_str());ImGui::SameLine(115);ImGui::ProgressBar(skins.empty()?0.f:(float)n/(float)skins.size(),ImVec2(std::max(100.f,w-140),10),"");} }
 void Detail(){if(skins.empty())return; if(ImGui::Button("Load official artwork"))LoadPreviewImage(); Preview();auto& s=skins[selected];ImGui::Text("Collection: %s",s.collection.c_str());int v=0;for(int i=0;i<(int)s.variants.size();i++)if(s.variant==s.variants[i])v=i;if(ImGui::BeginCombo("Variant",s.variant.c_str())){for(int i=0;i<(int)s.variants.size();i++)if(ImGui::Selectable(s.variants[i].c_str(),v==i)){s.variant=s.variants[i];Save();}ImGui::EndCombo();}if(ImGui::SliderInt("Evolution level",&s.level,1,s.maxLevel))Save();if(ImGui::Button("Apply Skin to Local Collection",ImVec2(-1,38))){for(auto& other:skins)if(other.weapon==s.weapon)other.applied=false;s.applied=true;Notify("Applied to local collection: "+s.name);Save();}ImGui::TextDisabled("No game process, account or inventory is modified.");}
 void Settings(){ImGui::Text("Preferences");if(ImGui::Checkbox("VSync",&vsync))Save();if(ImGui::SliderFloat("UI scale",&scale,.8f,1.5f,"%.2fx"))Save();ImGui::TextDisabled("Settings saved in AppData / VSkinPremium");ImGui::TextDisabled("Catalog is public; ownership and premium availability are not verified.");if(ImGui::Button("Refresh complete catalog (internet required)")){RefreshCatalog(true);Notify("Catalog currently contains "+std::to_string(skins.size())+" skins");}if(ImGui::Button("Reset settings")){vsync=true;scale=1;Save();Notify("Settings reset");}}
 void Draw(){Theme();ImGui::GetIO().FontGlobalScale=scale;ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);ImGui::Begin("VSkin",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings);ImGui::TextColored(ImVec4(1,.275f,.333f,1),"VSKIN PREMIUM");ImGui::SameLine();ImGui::TextDisabled("v1.0 | LOCAL MODE | DX11 READY");ImGui::Separator();ImGui::BeginChild("sidebar",ImVec2(178,0),ImGuiChildFlags_Borders);for(const char* p:{"Dashboard","Skin Changer","Weapons","Collection","Favorites","Settings","Logs"}){bool active=page==p;if(ImGui::Selectable(p,active,0,ImVec2(-1,37))){page=p;transition=0;}}ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("content",ImVec2(0,0));ImGui::SetWindowFontScale(1.25f);ImGui::Text("%s",page.c_str());ImGui::SetWindowFontScale(1);ImGui::Separator();if(page=="Dashboard")Dashboard();else if(page=="Skin Changer"){Detail();}else if(page=="Weapons")Catalog();else if(page=="Collection"){ImGui::TextDisabled("Locally applied skins");ImGui::BeginChild("localCollection");for(auto& skin:skins)if(skin.applied)ImGui::BulletText("%s - %s",skin.weapon.c_str(),skin.name.c_str());ImGui::EndChild();}else if(page=="Favorites")Catalog(true);else if(page=="Settings")Settings();else if(page=="Logs"){for(auto& l:logs)ImGui::BulletText("%s",l.c_str());}ImGui::EndChild();if(toastTimer>0){toastTimer-=ImGui::GetIO().DeltaTime;ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x-340,55));ImGui::SetNextWindowBgAlpha(.93f);ImGui::Begin("##toast",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs);ImGui::Text("%s",toast.c_str());ImGui::End();}ImGui::End();}
};
int WINAPI WinMain(HINSTANCE h,HINSTANCE,LPSTR,int){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);WNDCLASSEX wc={sizeof(wc),CS_CLASSDC,WndProc,0,0,h,nullptr,nullptr,nullptr,nullptr,L"VSkinPremium",nullptr};RegisterClassEx(&wc);HWND hwnd=CreateWindow(wc.lpszClassName,L"VSkin Premium - Standalone",WS_OVERLAPPEDWINDOW,100,100,1160,780,nullptr,nullptr,h,nullptr);if(!InitD3D(hwnd)){DestroyWindow(hwnd);UnregisterClass(wc.lpszClassName,h);return 1;}ShowWindow(hwnd,SW_SHOWDEFAULT);UpdateWindow(hwnd);IMGUI_CHECKVERSION();ImGui::CreateContext();ImGui::StyleColorsDark();ImGui_ImplWin32_Init(hwnd);ImGui_ImplDX11_Init(device,context);App app;MSG msg={};while(msg.message!=WM_QUIT){if(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessage(&msg);continue;}ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();app.Draw();ImGui::Render();const float clear[4]={.051f,.059f,.078f,1};context->OMSetRenderTargets(1,&target,nullptr);context->ClearRenderTargetView(target,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());swapchain->Present(app.vsync?1:0,0);}app.Save();ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext();CleanupTarget();for(auto& [id,view]:textures)if(view)view->Release();swapchain->Release();context->Release();device->Release();DestroyWindow(hwnd);UnregisterClass(wc.lpszClassName,h);CoUninitialize();return 0;}
