#include "TempestPresentationShader.h"

#include <cstring>
#include <iostream>

#include <d3dx8core.h>

int main()
{
    ID3DXBuffer *compiledShader = nullptr;
    ID3DXBuffer *errors = nullptr;
    const HRESULT result = D3DXAssembleShader(
        Tempest::Presentation::ShaderSource,
        static_cast<UINT>(std::strlen(Tempest::Presentation::ShaderSource)),
        0,
        nullptr,
        &compiledShader,
        &errors);
    if (errors) {
        std::cerr << static_cast<const char *>(errors->GetBufferPointer());
        errors->Release();
    }
    if (FAILED(result) || !compiledShader) {
        return 1;
    }
    compiledShader->Release();
    std::cout << "PASS: Project Tempest accessibility presentation shader assembled headlessly\n";
    return 0;
}
