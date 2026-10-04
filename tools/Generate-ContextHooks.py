"""Generate typed forwarding thunks from the installed Windows SDK, not guessed slots."""
import argparse,re
from pathlib import Path

def clean(text):
    text=re.sub(r'/\*.*?\*/','',text,flags=re.S)
    # SAL macros can contain nested parentheses and commas.
    pattern=re.compile(r'\b_[A-Za-z][A-Za-z0-9_]*_\b')
    while m:=pattern.search(text):
        end=m.end()
        if end<len(text) and text[end]=='(':
            depth=1;end+=1
            while depth:
                depth+=(text[end]=='(')-(text[end]==')');end+=1
        text=text[:m.start()]+text[end:]
    return ' '.join(text.split())

def generate(sdk,out):
    raw='\n'.join(p.read_text(encoding='utf-8') for p in sdk.parent.glob('d3d11*.h'))
    raw=raw.split('typedef struct ID3D11DeviceContext4Vtbl',1)[1].split('END_INTERFACE',1)[0]
    methods=[]
    for m in re.finditer(r'(\w+)\s*\(\s*STDMETHODCALLTYPE\s*\*(\w+)\s*\)\s*\((.*?)\)\s*;',raw,re.S):
        result,name,params=m.groups();params=clean(params)
        args=[re.search(r'(\w+)\s*(?:\[[^\]]*\])?$',p.strip()).group(1) for p in params.split(',')]
        methods.append((result,name,params,args))
    if len(methods)<140 or methods[12][1]!='DrawIndexed' or methods[-1][1]!='Wait':
        raise ValueError(f'Unexpected SDK vtable: {len(methods)} methods')
    out.mkdir(parents=True,exist_ok=True)
    header='#pragma once\nnamespace skyrim_mc::stream::slots {\n'
    for i,(_,name,_,_) in enumerate(methods):header+=f'inline constexpr unsigned {name}={i};\n'
    def category(name):
        if name in ('Map','Unmap'):return 'map-unmap'
        if name.startswith('Draw') or name.startswith('Dispatch'):return 'draw-dispatch'
        if name.startswith('UpdateSubresource') or name=='UpdateTiles':return 'update'
        if name in ('Begin','End','GetData','Flush','Flush1','Signal','Wait','FinishCommandList','ExecuteCommandList','TiledResourceBarrier'):return 'query-sync'
        if name.startswith(('Copy','Clear','Discard','Resolve','Generate','Resize')):return 'resource-copy-clear'
        if 'Get' in name and name not in ('GetDevice','GetPrivateData'):return 'getter'
        if name.startswith(('VSSet','PSSet','GSSet','HSSet','DSSet','CSSet','IASet','OMSet','RSSet','SOSet')) or name in ('SetPredication','SetResourceMinLOD','SwapDeviceContextState','SetHardwareProtectionState'):return 'state'
        return 'interface-other'
    header+=f'inline constexpr unsigned count={len(methods)};\n'
    header+='inline constexpr const char* names[]{'+','.join(f'"{name}"' for _,name,_,_ in methods)+'};\n'
    header+='inline constexpr const char* categories[]{'+','.join(f'"{category(name)}"' for _,name,_,_ in methods)+'};\n'
    header+='inline constexpr bool hresults[]{'+','.join('true' if result=='HRESULT' else 'false' for result,_,_,_ in methods)+'};\n}\n'
    (out/'context_slots.hpp').write_text(header,encoding='utf-8')
    cpp='''#define CINTERFACE
#define D3D11_NO_HELPERS
#define NOMINMAX
#include <d3d11_4.h>
#include <Windows.h>
#include <cstring>
#include "skyrim_mc/context_hooks.hpp"
namespace skyrim_mc::stream {
namespace {
ID3D11DeviceContext4* object=nullptr;
const ID3D11DeviceContext4Vtbl* original=nullptr;
ID3D11DeviceContext4Vtbl replacement{};
void* owner=nullptr;Callbacks callbacks{};
struct Lock {Lock(){callbacks.enter(owner);}~Lock(){callbacks.leave(owner);}};
'''
    special={'DrawIndexed','Map','Unmap','UpdateSubresource','UpdateSubresource1','CopyResource','CopySubresourceRegion','CopySubresourceRegion1','Begin','End'}
    for i,(result,name,params,args) in enumerate(methods):
        cpp+=f'{result} STDMETHODCALLTYPE hooked_{name}({params}) {{\n    Lock lock;\n'
        use='nullptr'
        if name in special:
            cpp+='    const std::uintptr_t values[]{'+','.join(f'(std::uintptr_t){a}' for a in args[1:])+'};\n';use='values'
        cpp+=f'    const bool skip=callbacks.before(owner,{i},{use});\n'
        result_value='reinterpret_cast<std::intptr_t>(result)' if '*' in result else 'static_cast<std::intptr_t>(result)' if result!='void' else '0'
        if name=='DrawIndexed':
            cpp+=f'    if(!skip) {{const auto timing=callbacks.start_original(owner,{i},{use});original->{name}('+','.join(args)+f');callbacks.finish_original(owner,{i},timing,0);}}\n'
        else:
            cpp+='    (void)skip;\n'
            cpp+=f'    const auto timing=callbacks.start_original(owner,{i},{use});\n'
            cpp+=f'    {"const auto result=" if result!="void" else ""}original->{name}('+','.join(args)+');\n'
            cpp+=f'    callbacks.finish_original(owner,{i},timing,{result_value});\n'
        cpp+=f'    callbacks.after(owner,{i},{use},{result_value});\n'
        if result!='void':cpp+='    return result;\n'
        cpp+='}\n'
    cpp+='''}
bool attach_context(void* pointer,void* state,Callbacks cb) noexcept {
    if(object || !pointer || !state) return false;
    auto* context=static_cast<ID3D11DeviceContext4*>(pointer);
    ID3D11DeviceContext4* extended=nullptr;
    const auto* table=context->lpVtbl;
    if(FAILED(table->QueryInterface(context,IID_ID3D11DeviceContext4,(void**)&extended))) return false;
    const bool same=extended==context;
    extended->lpVtbl->Release(extended);
    if(!same || table->GetType(context)!=D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
    original=table;replacement=*table;owner=state;callbacks=cb;
'''
    for _,name,_,_ in methods:cpp+=f'    replacement.{name}=&hooked_{name};\n'
    cpp+='''    object=context;
    if(InterlockedCompareExchangePointer((void* volatile*)context,&replacement,(void*)table)!=(void*)table) {object=nullptr;return false;}
    return true;
}
void detach_context() noexcept {
    if(object) {InterlockedCompareExchangePointer((void* volatile*)object,(void*)original,&replacement);object=nullptr;}
}
}
'''
    (out/'context_hooks.cpp').write_text(cpp,encoding='utf-8')
    print(f'Generated {len(methods)} SDK-typed context forwarding methods')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('sdk',type=Path);p.add_argument('output',type=Path);a=p.parse_args();generate(a.sdk,a.output)
