#include "d3d11_post_processor.h"

#include "config.h"
#include "d3d11_cas_upscaler.h"
#include "d3d11_fsr_upscaler.h"
#include "d3d11_nis_upscaler.h"
#include "logging.h"
#include "hooks.h"

#include "shader_hrm_fullscreen_tri.h"
#include "shader_hrm_mask.h"
#include "shader_rdm_mask.h"
#include "shader_rdm_reconstruction.h"

#include <sstream>

namespace vrperfkit {
	D3D11PostProcessor::D3D11PostProcessor(ComPtr<ID3D11Device> device) : device(device) {
		enableDynamic = g_config.hiddenMask.dynamic || g_config.ffr.dynamic;

		is_rdm = (g_config.ffr.enabled && g_config.ffr.method == FixedFoveatedMethod::RDM);
		if (is_rdm) {
			hiddenMaskApply = g_config.ffr.enabled;
			preciseResolution = g_config.ffr.preciseResolution;
			ignoreFirstTargetRenders = g_config.ffr.ignoreFirstTargetRenders;
			ignoreLastTargetRenders = g_config.ffr.ignoreLastTargetRenders;
			renderOnlyTarget = g_config.ffr.renderOnlyTarget;
			edgeRadius = g_config.ffr.edgeRadius;
		} else {
			hiddenMaskApply = g_config.hiddenMask.enabled;
			preciseResolution = g_config.hiddenMask.preciseResolution;
			ignoreFirstTargetRenders = g_config.hiddenMask.ignoreFirstTargetRenders;
			ignoreLastTargetRenders = g_config.hiddenMask.ignoreLastTargetRenders;
			renderOnlyTarget = g_config.hiddenMask.renderOnlyTarget;
			edgeRadius = g_config.hiddenMask.edgeRadius;
		}

		device->GetImmediateContext(context.GetAddressOf());
		LOG_INFO << "Init PostProcessor";
	}

	HRESULT D3D11PostProcessor::ClearDepthStencilView(ID3D11DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) {
		if (pDepthStencilView == nullptr) {
			return 0;
		}

		ComPtr<ID3D11Resource> resource;
		pDepthStencilView->GetResource(resource.GetAddressOf());
		if (resource.Get() == nullptr) {
			return 0;
		}

		D3D11_TEXTURE2D_DESC texDesc;
		((ID3D11Texture2D*)resource.Get())->GetDesc(&texDesc);
		
		if (preciseResolution) {
			if (texDesc.Width != textureWidth || texDesc.Height != textureHeight) {
				return 0;
			}
		} else if (texDesc.Width < textureWidth || texDesc.Height < textureHeight || texDesc.Width == texDesc.Height) {
			// smaller than submitted texture size, so not the correct render target
			// if equals, this is probably the shadow map or something similar
			return 0;
		}
		
		ApplyRadialDensityMask((ID3D11Texture2D*)resource.Get(), Depth, Stencil);

		return 0;
	}

	struct RdmMaskingConstants {
		float depthOut;
		float radius[3];
		float invClusterResolution[2];
		float projectionCenter[2];
		float yFix[2];
		float edgeRadius;
		float _padding;
	};

	struct RdmReconstructConstants {
		int offset[2];
		float projectionCenter[2];
		float invClusterResolution[2];
		float invResolution[2];
		float radius[3];
		float edgeRadius;
	};

	DXGI_FORMAT TranslateTypelessDepthFormats(DXGI_FORMAT format) {
		switch (format) {
		case DXGI_FORMAT_R16_TYPELESS:
			return DXGI_FORMAT_D16_UNORM;
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
			return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case DXGI_FORMAT_R32_TYPELESS:
			return DXGI_FORMAT_D32_FLOAT;
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
			return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
		default:
			return format;
		}
	}

	DXGI_FORMAT DetermineOutputFormat(DXGI_FORMAT inputFormat) {
		switch (inputFormat) {
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			// SteamVR applies a different color conversion for these formats that we can't match
			// with R8G8B8 textures, so we have to use a matching texture format for our own resources.
			// Otherwise we'll get darkened pictures (applies to Revive mostly)
			return DXGI_FORMAT_R10G10B10A2_UNORM;
		default:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		}
	}

	bool IsConsideredSrgbByOpenVR(DXGI_FORMAT format) {
		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return true;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			// OpenVR appears to treat submitted typeless textures as SRGB
			return true;
		default:
			return false;
		}
	}

	void D3D11PostProcessor::SetProjCenters(float LX, float LY, float RX, float RY) {
		projX[0] = LX;
		projY[0] = LY;
		projX[1] = RX;
		projY[1] = RY;
	}

	//void D3D11PostProcessor::PrepareResources(ID3D11Texture2D *inputTexture, vr::EColorSpace colorSpace) {
	void D3D11PostProcessor::PrepareResources(ID3D11Texture2D *inputTexture) {
		LOG_INFO << "Creating post-processing resources";
		inputTexture->GetDevice( device.GetAddressOf() );
		device->GetImmediateContext( context.GetAddressOf() );

		D3D11_TEXTURE2D_DESC std;
		inputTexture->GetDesc( &std );
		//inputIsSrgb = colorSpace == vr::ColorSpace_Gamma || (colorSpace == vr::ColorSpace_Auto && IsConsideredSrgbByOpenVR(std.Format));
		inputIsSrgb = false;
		if (inputIsSrgb) {
			LOG_INFO << "Input texture is in SRGB color space";
		}

		textureWidth = std.Width;
		textureHeight = std.Height;

		D3D11_SAMPLER_DESC sd;
		sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
		sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.MipLODBias = 0;
		sd.MaxAnisotropy = 1;
		sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sd.MinLOD = 0;
		sd.MaxLOD = 0;
		device->CreateSamplerState(&sd, sampler.GetAddressOf());

		if (!(std.BindFlags & D3D11_BIND_SHADER_RESOURCE) || std.SampleDesc.Count > 1 || IsSrgbFormat(std.Format)) {
			LOG_INFO << "Input texture can't be bound directly, need to copy";
			requiresCopy = true;
			PrepareCopyResources(std.Format);
		}

		DXGI_FORMAT textureFormat = DetermineOutputFormat(std.Format);
		PrepareRdmResources(textureFormat);

		hrmInitialized = true;
	}

	void D3D11PostProcessor::PrepareCopyResources(DXGI_FORMAT format) {
		LOG_INFO << "Creating copy texture of size " << textureWidth << "x" << textureHeight;
		D3D11_TEXTURE2D_DESC td;
		td.Width = textureWidth;
		td.Height = textureHeight;
		td.MipLevels = 1;
		td.CPUAccessFlags = 0;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		td.Format = MakeSrgbFormatsTypeless(format);
		td.MiscFlags = 0;
		td.SampleDesc.Count = 1;
		td.SampleDesc.Quality = 0;
		td.ArraySize = 1;
		CheckResult("Creating copy texture", device->CreateTexture2D( &td, nullptr, copiedTexture.GetAddressOf()));
		D3D11_SHADER_RESOURCE_VIEW_DESC srv;
		srv.Format = TranslateTypelessFormats(td.Format);
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = 1;
		srv.Texture2D.MostDetailedMip = 0;
		CheckResult("Creating copy SRV", device->CreateShaderResourceView(copiedTexture.Get(), &srv, copiedTextureView.GetAddressOf()));
	}

	void D3D11PostProcessor::PrepareRdmResources(DXGI_FORMAT format) {
		CheckResult("Creating HRM/RDM fullscreen tri vertex shader", device->CreateVertexShader( g_HRM_FullscreenTriShader, sizeof( g_HRM_FullscreenTriShader ), nullptr, hrmFullTriVertexShader.GetAddressOf() ));
		if (is_rdm) {
			CheckResult("Creating RDM masking shader", device->CreatePixelShader( g_RDM_MaskShader, sizeof( g_RDM_MaskShader ), nullptr, rdmMaskingShader.GetAddressOf() ));
			CheckResult("Creating RDM reconstruction shader", device->CreateComputeShader( g_RDM_ReconstructionShader, sizeof( g_RDM_ReconstructionShader ), nullptr, rdmReconstructShader.GetAddressOf() ));

			D3D11_TEXTURE2D_DESC td;
			td.Width = textureWidth;
			td.Height = textureHeight;
			td.MipLevels = 1;
			td.CPUAccessFlags = 0;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
			td.Format = format;
			td.MiscFlags = 0;
			td.SampleDesc.Count = 1;
			td.SampleDesc.Quality = 0;
			td.ArraySize = 1;
			CheckResult("Creating RDM reconstructed texture", device->CreateTexture2D( &td, nullptr, rdmReconstructedTexture.GetAddressOf() ));
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav;
			uav.Format = td.Format;
			uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uav.Texture2D.MipSlice = 0;
			CheckResult("Creating RDM reconstructed UAV", device->CreateUnorderedAccessView( rdmReconstructedTexture.Get(), &uav, rdmReconstructedUav.GetAddressOf() ));
			D3D11_SHADER_RESOURCE_VIEW_DESC svd;
			svd.Format = TranslateTypelessFormats(format);
			svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			svd.Texture2D.MostDetailedMip = 0;
			svd.Texture2D.MipLevels = 1;
			CheckResult("Creating RDM reconstructed view", device->CreateShaderResourceView( rdmReconstructedTexture.Get(), &svd, rdmReconstructedView.GetAddressOf() ));

		} else {
			CheckResult("Creating HRM masking shader", device->CreatePixelShader( g_HRM_MaskShader, sizeof( g_HRM_MaskShader ), nullptr, hrmMaskingShader.GetAddressOf() ));
		}

		D3D11_DEPTH_STENCIL_DESC dsd;
		dsd.DepthEnable = TRUE;
		dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		dsd.DepthFunc = D3D11_COMPARISON_ALWAYS;
		dsd.StencilEnable = TRUE;
		dsd.StencilReadMask = 255;
		dsd.StencilWriteMask = 255;
		dsd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
		dsd.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		dsd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		dsd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		dsd.BackFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
		dsd.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		dsd.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		dsd.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		CheckResult("Creating HRM depth stencil state", device->CreateDepthStencilState(&dsd, hrmDepthStencilState.GetAddressOf()));

		D3D11_RASTERIZER_DESC rsd;
		rsd.FillMode = D3D11_FILL_SOLID;
		rsd.CullMode = D3D11_CULL_NONE;
		rsd.FrontCounterClockwise = FALSE;
		rsd.DepthBias = 0;
		rsd.SlopeScaledDepthBias = 0;
		rsd.DepthBiasClamp = 0;
		rsd.DepthClipEnable = TRUE;
		rsd.ScissorEnable = FALSE;
		rsd.MultisampleEnable = FALSE;
		rsd.AntialiasedLineEnable = FALSE;
		CheckResult("Creating HRM rasterizer state", device->CreateRasterizerState(&rsd, hrmRasterizerState.GetAddressOf()));

		D3D11_BUFFER_DESC bd;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bd.MiscFlags = 0;
		bd.StructureByteStride = 0;
		bd.ByteWidth = sizeof(RdmMaskingConstants);
		CheckResult("Creating HRM masking constants buffer", device->CreateBuffer( &bd, nullptr, hrmMaskingConstantsBuffer[0].GetAddressOf() ));
		CheckResult("Creating HRM masking constants buffer", device->CreateBuffer( &bd, nullptr, hrmMaskingConstantsBuffer[1].GetAddressOf() ));

		if (is_rdm) {
			bd.ByteWidth = sizeof(RdmReconstructConstants);
			CheckResult("Creating RDM reconstruct constants buffer", device->CreateBuffer( &bd, nullptr, rdmReconstructConstantsBuffer[0].GetAddressOf() ));
			CheckResult("Creating RDM reconstruct constants buffer", device->CreateBuffer( &bd, nullptr, rdmReconstructConstantsBuffer[1].GetAddressOf() ));
		}
	}

	bool D3D11PostProcessor::HasBlacklistedTextureName(ID3D11Texture2D *tex) {
		// Used to ignore certain depth textures that we know are not relevant for us
		// currently found in some older Unity games
		char debugName[255] = { 0 };
		UINT bufferSize = 255;
		tex->GetPrivateData( WKPDID_D3DDebugObjectName, &bufferSize, debugName );
		if (strncmp( debugName, "Camera DepthTexture", 255 ) == 0) {
			return true;
		}
		return false;
	}

	ID3D11DepthStencilView * D3D11PostProcessor::GetDepthStencilView( ID3D11Texture2D *depthStencilTex, vr::EVREye eye ) {
		if ( depthStencilViews.find( depthStencilTex ) == depthStencilViews.end() ) {
			LOG_INFO << "Creating depth stencil views for " << std::hex << depthStencilTex << std::dec;
			D3D11_TEXTURE2D_DESC td;
			depthStencilTex->GetDesc( &td );
			bool isArray = td.ArraySize == 2;
			bool isMS = td.SampleDesc.Count > 1;
			LOG_INFO << "Texture format " << td.Format << ", array size " << td.ArraySize << ", sample count " << td.SampleDesc.Count;
			D3D11_DEPTH_STENCIL_VIEW_DESC dvd;
			dvd.Format = TranslateTypelessDepthFormats( td.Format );
			dvd.ViewDimension = isMS ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
			dvd.Flags = 0;
			dvd.Texture2D.MipSlice = 0;
			auto &views = depthStencilViews[depthStencilTex];
			HRESULT result = device->CreateDepthStencilView( depthStencilTex, &dvd, views.view[0].GetAddressOf() );
			if (FAILED(result)) {
				LOG_ERROR << "Error creating depth stencil view: " << std::hex << result;
				return nullptr;
			}
			if (isArray) {
				LOG_INFO << "Depth stencil texture is an array, using separate slice per eye\n";
				if (isMS) {
					dvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
					dvd.Texture2DMSArray.ArraySize = 1;
					dvd.Texture2DMSArray.FirstArraySlice = D3D11CalcSubresource( 0, 1, 1 );
				} else {
					dvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
					dvd.Texture2DArray.MipSlice = 0;
					dvd.Texture2DArray.ArraySize = 1;
					dvd.Texture2DArray.FirstArraySlice = D3D11CalcSubresource( 0, 1, 1 );
				}
				result = device->CreateDepthStencilView( depthStencilTex, &dvd, views.view[1].GetAddressOf() );
				if (FAILED(result)) {
					LOG_ERROR << "Error creating depth stencil view array slice: " << std::hex << result;
					return nullptr;
				}
			} else {
				views.view[1] = views.view[0];
			}
		}

		return depthStencilViews[depthStencilTex].view[eye].Get();
	}

	void D3D11PostProcessor::ApplyRadialDensityMask(ID3D11Texture2D *depthStencilTex, float depth, uint8_t stencil) {
		if (HasBlacklistedTextureName(depthStencilTex)) {
			return;
		}

		++depthClearCount;

		if (!hiddenMaskApply) {
			return;
		}

		vr::EVREye currentEye = vr::Eye_Left;
		
		if ((renderOnlyTarget > 0 && renderOnlyTarget != depthClearCount) || (renderOnlyTarget < 0 && depthClearCountMax + 1 + renderOnlyTarget != depthClearCount)) {
			if (g_config.ffrFastModeUsesHRMCount) {
				g_config.ffrApplyFastMode = false;
			}
			return;
		}

		if (depthClearCount <= ignoreFirstTargetRenders || (ignoreLastTargetRenders > 0 && depthClearCount > depthClearCountMax - ignoreLastTargetRenders)) {
			if (g_config.ffrFastModeUsesHRMCount) {
				g_config.ffrApplyFastMode = false;
			}
			return;
		}

		if (g_config.ffrFastModeUsesHRMCount) {
			g_config.ffrApplyFastMode = true;
		}

		if (g_config.gameMode == GameMode::LEFT_EYE_FIRST) {
			if (g_config.renderingSecondEye) {
				currentEye = vr::Eye_Right;
			}

		} else if (g_config.gameMode == GameMode::RIGHT_EYE_FIRST) {
			if (!g_config.renderingSecondEye) {
				currentEye = vr::Eye_Right;
			}
		}

		D3D11_TEXTURE2D_DESC td;
		depthStencilTex->GetDesc( &td );

		bool sideBySide = g_config.gameMode == GameMode::GENERIC_SINGLE || td.Width >= 2 * textureWidth;
		bool arrayTex = td.ArraySize == 2;

		if (!sideBySide && !arrayTex && g_config.renderingSecondEye) {
			currentEye = vr::Eye_Right;
		}

		//LOG_INFO << "Frame: " << depthClearCount << " Eye: " << g_config.renderingSecondEye;

		uint32_t renderWidth = td.Width * (sideBySide ? 0.5 : 1);
		uint32_t renderHeight = td.Height;

		// Store D3D11 State before drawing mask
		ComPtr<ID3D11VertexShader> prevVS;
		context->VSGetShader(prevVS.GetAddressOf(), nullptr, nullptr);
		ComPtr<ID3D11PixelShader> prevPS;
		context->PSGetShader(prevPS.GetAddressOf(), nullptr, nullptr);
		ComPtr<ID3D11InputLayout> inputLayout;
		context->IAGetInputLayout( inputLayout.GetAddressOf() );
		D3D11_PRIMITIVE_TOPOLOGY topology;
		context->IAGetPrimitiveTopology( &topology );
		ID3D11Buffer *vertexBuffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		UINT strides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		UINT offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		context->IAGetVertexBuffers( 0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, vertexBuffers, strides, offsets );
		ComPtr<ID3D11Buffer> indexBuffer;
		DXGI_FORMAT format;
		UINT offset;
		context->IAGetIndexBuffer(indexBuffer.GetAddressOf(), &format, &offset);
		ID3D11RenderTargetView *renderTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
		ComPtr<ID3D11DepthStencilView> depthStencil;
		context->OMGetRenderTargets( D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, renderTargets, depthStencil.GetAddressOf() );
		ComPtr<ID3D11RasterizerState> rasterizerState;
		context->RSGetState( rasterizerState.GetAddressOf() );
		ComPtr<ID3D11DepthStencilState> depthStencilState;
		UINT stencilRef;
		context->OMGetDepthStencilState( depthStencilState.GetAddressOf(), &stencilRef );
		D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
		UINT numViewports = 0;
		context->RSGetViewports( &numViewports, nullptr );
		context->RSGetViewports( &numViewports, viewports );
		ComPtr<ID3D11Buffer> vsConstantBuffer;
		context->VSGetConstantBuffers( 0, 1, vsConstantBuffer.GetAddressOf() );
		ComPtr<ID3D11Buffer> psConstantBuffer;
		context->PSGetConstantBuffers( 0, 1, psConstantBuffer.GetAddressOf() );
		
		context->VSSetShader( hrmFullTriVertexShader.Get(), nullptr, 0 );
		if (is_rdm) {
			context->PSSetShader( rdmMaskingShader.Get(), nullptr, 0 );
		} else {
			context->PSSetShader( hrmMaskingShader.Get(), nullptr, 0 );
		}
		context->IASetInputLayout( nullptr );
		context->IASetPrimitiveTopology( D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		context->IASetVertexBuffers( 0, 0, nullptr, nullptr, nullptr );
		context->IASetIndexBuffer( nullptr, DXGI_FORMAT_UNKNOWN, 0 );
		context->OMSetRenderTargets( 0, nullptr, GetDepthStencilView(depthStencilTex, currentEye) );
		context->RSSetState(hrmRasterizerState.Get());
		context->OMSetDepthStencilState(hrmDepthStencilState.Get(), ~stencil);

		RdmMaskingConstants constants;
		constants.depthOut = 1.f - depth;
		if (is_rdm) {
			constants.radius[0] = g_config.ffr.innerRadius;
			constants.radius[1] = g_config.ffr.midRadius;
			constants.radius[2] = g_config.ffr.outerRadius;
		}
		constants.edgeRadius = edgeRadius;
		constants.invClusterResolution[0] = 8.f / renderWidth;
		constants.invClusterResolution[1] = 8.f / renderHeight;
		constants.projectionCenter[0] = projX[currentEye];
		constants.projectionCenter[1] = projY[currentEye];
		// New Unity engine with array textures renders heads down and then flips the texture before submitting.
		// so we also need to construct the RDM heads-down in that case.
		constants.yFix[0] = arrayTex ? -1 : 1;
		constants.yFix[1] = arrayTex ? renderHeight : 0;
		D3D11_MAPPED_SUBRESOURCE mapped { nullptr, 0, 0 };
		context->Map( hrmMaskingConstantsBuffer[currentEye].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
		memcpy(mapped.pData, &constants, sizeof(constants));
		context->Unmap( hrmMaskingConstantsBuffer[currentEye].Get(), 0 );
		context->VSSetConstantBuffers( 0, 1, hrmMaskingConstantsBuffer[currentEye].GetAddressOf() );
		context->PSSetConstantBuffers( 0, 1, hrmMaskingConstantsBuffer[currentEye].GetAddressOf() );

		D3D11_VIEWPORT vp;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		vp.MinDepth = 0;
		vp.MaxDepth = 1;
		vp.Width = renderWidth;
		vp.Height = renderHeight;
		context->RSSetViewports( 1, &vp );

		context->Draw( 3, 0 );

		if (sideBySide || arrayTex) {
			constants.projectionCenter[0] = projX[vr::Eye_Right] + (sideBySide ? 1.f : 0.f);
			constants.projectionCenter[1] = projY[vr::Eye_Right];
			context->Map( hrmMaskingConstantsBuffer[vr::Eye_Right].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
			memcpy(mapped.pData, &constants, sizeof(constants));
			context->Unmap( hrmMaskingConstantsBuffer[vr::Eye_Right].Get(), 0 );
			context->VSSetConstantBuffers( 0, 1, hrmMaskingConstantsBuffer[1].GetAddressOf() );
			context->PSSetConstantBuffers( 0, 1, hrmMaskingConstantsBuffer[1].GetAddressOf() );
			context->OMSetRenderTargets( 0, nullptr, GetDepthStencilView(depthStencilTex, vr::Eye_Right) );
			if (sideBySide) {
				vp.TopLeftX = renderWidth;
			}
			context->RSSetViewports( 1, &vp );

			context->Draw( 3, 0 );
		}
		
		// Restore D3D11 State
		context->VSSetShader(prevVS.Get(), nullptr, 0);
		context->PSSetShader(prevPS.Get(), nullptr, 0);
		context->IASetInputLayout( inputLayout.Get() );
		context->IASetPrimitiveTopology( topology );
		context->IASetVertexBuffers( 0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, vertexBuffers, strides, offsets );
		for (int i = 0; i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i) {
			if (vertexBuffers[i])
				vertexBuffers[i]->Release();
		}
		context->IASetIndexBuffer( indexBuffer.Get(), format, offset );
		context->OMSetRenderTargets( D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, renderTargets, depthStencil.Get() );
		for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
			if (renderTargets[i]) {
				renderTargets[i]->Release();
			}
		}
		context->RSSetState( rasterizerState.Get() );
		context->OMSetDepthStencilState( depthStencilState.Get(), stencilRef );
		context->RSSetViewports( numViewports, viewports );
		context->VSSetConstantBuffers( 0, 1, vsConstantBuffer.GetAddressOf() );
		context->PSSetConstantBuffers( 0, 1, psConstantBuffer.GetAddressOf() );
		
	}

	void D3D11PostProcessor::ReconstructRdmRender(const D3D11PostProcessInput &input) {
		context->CSSetShader( rdmReconstructShader.Get(), nullptr, 0 );
		ID3D11Buffer *emptyBind[] = {nullptr};
		context->CSSetConstantBuffers( 0, 1, emptyBind );

		RdmReconstructConstants constants;
		constants.offset[0] = input.inputViewport.x;
		constants.offset[1] = input.inputViewport.y;
		constants.projectionCenter[0] = projX[input.eye];
		constants.projectionCenter[1] = projY[input.eye];
		constants.invResolution[0] = 1.f / textureWidth;
		constants.invResolution[1] = 1.f / textureHeight;
		constants.invClusterResolution[0] = 8.f / input.inputViewport.width;
		constants.invClusterResolution[1] = 8.f / input.inputViewport.height;
		constants.radius[0] = g_config.ffr.innerRadius;
		constants.radius[1] = g_config.ffr.midRadius;
		constants.radius[2] = g_config.ffr.outerRadius;
		constants.edgeRadius = edgeRadius;
		if (g_config.gameMode == GameMode::GENERIC_SINGLE && input.eye == vr::Eye_Right) {
			constants.projectionCenter[0] += 1.f;
		}
		D3D11_MAPPED_SUBRESOURCE mapped { nullptr, 0, 0 };
		context->Map( rdmReconstructConstantsBuffer[input.eye].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
		memcpy(mapped.pData, &constants, sizeof(constants));
		context->Unmap( rdmReconstructConstantsBuffer[input.eye].Get(), 0 );
		UINT uavCount = -1;
		context->CSSetUnorderedAccessViews( 0, 1, rdmReconstructedUav.GetAddressOf(), &uavCount );
		context->CSSetConstantBuffers( 0, 1, rdmReconstructConstantsBuffer[input.eye].GetAddressOf() );
		ID3D11ShaderResourceView *srvs[1] = {input.inputView};
		context->CSSetShaderResources( 0, 1, srvs );
		context->CSSetSamplers(0, 1, sampler.GetAddressOf());
		context->Dispatch( (input.inputViewport.width + 7) / 8, (input.inputViewport.height + 7) / 8, 1 );
	}

	bool D3D11PostProcessor::Apply(const D3D11PostProcessInput &input, Viewport &outputViewport) {
		bool didPostprocessing = false;
/*
		if (g_config.debugMode) {
			StartProfiling();
		}
*/

		if (g_config.hiddenMask.enabled || is_rdm) {
			if (!hrmInitialized) {
				try {
					PrepareResources(input.inputTexture);
				} catch (...) {
					LOG_ERROR << "Resource creation failed, disabling";
					return false;
				}
			}
		}
		
		if (g_config.upscaling.enabled) {
			try {
				D3D11State previousState;
				StoreD3D11State(context.Get(), previousState);

				// Disable any RTs in case our input texture is still bound; otherwise using it as a view will fail
				context->OMSetRenderTargets(0, nullptr, nullptr);

				PrepareUpscaler(input.outputTexture);
				D3D11_TEXTURE2D_DESC td;
				input.outputTexture->GetDesc(&td);
				outputViewport.x = outputViewport.y = 0;
				outputViewport.width = td.Width;
				outputViewport.height = td.Height;
				if (input.mode == TextureMode::COMBINED) {
					outputViewport.width /= 2;
					if (input.eye == RIGHT_EYE) {
						outputViewport.x += outputViewport.width;
					}
				}

				if (is_rdm) {
					ReconstructRdmRender(input);
					context->CopyResource(input.inputTexture, rdmReconstructedTexture.Get());
				}
			
				upscaler->Upscale(input, outputViewport);

				float newLodBias = -log2f(outputViewport.width / (float)input.inputViewport.width);
				if (newLodBias != mipLodBias) {
					LOG_DEBUG << "MIP LOD Bias changed from " << mipLodBias << " to " << newLodBias << ", recreating samplers";
					passThroughSamplers.clear();
					mappedSamplers.clear();
					mipLodBias = newLodBias;
				}

				RestoreD3D11State(context.Get(), previousState);

				didPostprocessing = true;
			}
			catch (const std::exception &e) {
				LOG_ERROR << "Upscaling failed: " << e.what();
				g_config.upscaling.enabled = false;
			}
		}

		g_config.renderingSecondEye = !g_config.renderingSecondEye;
		g_config.ffrRenderTargetCountMax = g_config.ffrRenderTargetCount;
		g_config.ffrRenderTargetCount = 0;
		depthClearCountMax = depthClearCount;
		depthClearCount = 0;

		if (enableDynamic && (g_config.renderingSecondEye || g_config.gameMode == GameMode::GENERIC_SINGLE)) {
			EndDynamicProfiling();
		}
/*
		if (g_config.debugMode) {
			EndProfiling();
		}
*/
		return didPostprocessing;
	}

	bool D3D11PostProcessor::PrePSSetSamplers(UINT startSlot, UINT numSamplers, ID3D11SamplerState * const *ppSamplers) {
		if (!g_config.upscaling.applyMipBias) {
			passThroughSamplers.clear();
			mappedSamplers.clear();
			return false;
		}

		ID3D11SamplerState *samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
		memcpy(samplers, ppSamplers, numSamplers * sizeof(ID3D11SamplerState*));
		for (UINT i = 0; i < numSamplers; ++i) {
			ID3D11SamplerState *orig = samplers[i];
			if (orig == nullptr || passThroughSamplers.find(orig) != passThroughSamplers.end()) {
				continue;
			}

			if (mappedSamplers.find(orig) == mappedSamplers.end()) {
				D3D11_SAMPLER_DESC sd;
				orig->GetDesc(&sd);
				if (sd.MipLODBias != 0 || sd.MaxAnisotropy == 1) {
					// Do not mess with samplers that already have a bias or are not doing anisotropic filtering.
					// should hopefully reduce the chance of causing rendering errors.
					passThroughSamplers.insert(orig);
					continue;
				}
				sd.MipLODBias = mipLodBias;
				LOG_INFO << "Creating replacement sampler for " << orig << " with MIP LOD bias " << sd.MipLODBias;
				device->CreateSamplerState(&sd, mappedSamplers[orig].GetAddressOf());
				passThroughSamplers.insert(mappedSamplers[orig].Get());
			}

			samplers[i] = mappedSamplers[orig].Get();
		}

		context->PSSetSamplers(startSlot, numSamplers, samplers);
		return true;
	}

	void D3D11PostProcessor::PrepareUpscaler(ID3D11Texture2D *outputTexture) {
		if (upscaler == nullptr || upscaleMethod != g_config.upscaling.method) {
			D3D11_TEXTURE2D_DESC td;
			outputTexture->GetDesc(&td);
			upscaleMethod = g_config.upscaling.method;
			switch (upscaleMethod) {
			case UpscaleMethod::FSR:
				upscaler.reset(new D3D11FsrUpscaler(device.Get(), td.Width, td.Height, td.Format));
				break;
			case UpscaleMethod::NIS:
				upscaler.reset(new D3D11NisUpscaler(device.Get()));
				break;
			case UpscaleMethod::CAS:
				upscaler.reset(new D3D11CasUpscaler(device.Get()));
				break;
			}

			passThroughSamplers.clear();
			mappedSamplers.clear();
		}
	}


	void D3D11PostProcessor::CreateDynamicProfileQueries() {
		for (auto &profileQuery : dynamicProfileQueries) {
			D3D11_QUERY_DESC qd;
			qd.Query = D3D11_QUERY_TIMESTAMP;
			qd.MiscFlags = 0;
			device->CreateQuery(&qd, profileQuery.queryStart.ReleaseAndGetAddressOf());
			device->CreateQuery(&qd, profileQuery.queryEnd.ReleaseAndGetAddressOf());
			qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
			device->CreateQuery(&qd, profileQuery.queryDisjoint.ReleaseAndGetAddressOf());
		}
	}

	void D3D11PostProcessor::StartDynamicProfiling() {
		++DynamicSleepCount;
		if (DynamicSleepCount < g_config.dynamicFramesCheck) {
			return;
		}

		is_DynamicProfiling = true;

		DynamicSleepCount = 0;

		if (dynamicProfileQueries[0].queryStart == nullptr) {
			CreateDynamicProfileQueries();
		}

		context->Begin(dynamicProfileQueries[DynamicCurrentQuery].queryDisjoint.Get());
		context->End(dynamicProfileQueries[DynamicCurrentQuery].queryStart.Get());
	}

	void D3D11PostProcessor::EndDynamicProfiling() {
		if (is_DynamicProfiling) {
			context->End(dynamicProfileQueries[DynamicCurrentQuery].queryEnd.Get());
			context->End(dynamicProfileQueries[DynamicCurrentQuery].queryDisjoint.Get());

			DynamicCurrentQuery = (DynamicCurrentQuery + 1) % DYNAMIC_QUERY_COUNT;
			while (context->GetData(dynamicProfileQueries[0].queryDisjoint.Get(), nullptr, 0, 0) == S_FALSE) {
				Sleep(1);
			}
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
			HRESULT result = context->GetData(dynamicProfileQueries[DynamicCurrentQuery].queryDisjoint.Get(), &disjoint, sizeof(disjoint), 0);
			if (result == S_OK && !disjoint.Disjoint) {
				UINT64 begin, end;
				context->GetData(dynamicProfileQueries[DynamicCurrentQuery].queryStart.Get(), &begin, sizeof(UINT64), 0);
				context->GetData(dynamicProfileQueries[DynamicCurrentQuery].queryEnd.Get(), &end, sizeof(UINT64), 0);
				float frameTime = (end - begin) / float(disjoint.Frequency);	// FrameTime in seconds

				//LOG_INFO << "frameTime: " << std::setprecision(8) << frameTime;

				// HRM
				if (g_config.hiddenMask.dynamic) {
					if (frameTime > g_config.hiddenMask.targetFrameTime) {
						if (g_config.hiddenMask.dynamicChangeRadius) {
							if ((edgeRadius - g_config.hiddenMask.decreaseRadiusStep) >= g_config.hiddenMask.minRadius) {
								edgeRadius -= g_config.hiddenMask.decreaseRadiusStep;
							}
						} else {
							hiddenMaskApply = true;
						}
					} else if (frameTime < g_config.hiddenMask.marginFrameTime) {
						if (g_config.hiddenMask.dynamicChangeRadius) {
							if ((edgeRadius + g_config.hiddenMask.increaseRadiusStep) <= g_config.hiddenMask.maxRadius) {
								edgeRadius += g_config.hiddenMask.increaseRadiusStep;
							}
						} else {
							hiddenMaskApply = false;
						}
					}
				}

				// FFR
				if (g_config.ffr.dynamic) {
					if (frameTime > g_config.ffr.targetFrameTime) {
						if (g_config.ffr.dynamicChangeRadius) {
							if ((g_config.ffr.innerRadius - g_config.ffr.decreaseRadiusStep) >= g_config.ffr.minRadius) {
								g_config.ffr.innerRadius -= g_config.ffr.decreaseRadiusStep;
								g_config.ffr.midRadius -= g_config.ffr.decreaseRadiusStep;
								g_config.ffr.outerRadius -= g_config.ffr.decreaseRadiusStep;
								g_config.ffr.radiusChanged[0] = true;
								g_config.ffr.radiusChanged[1] = true;
							}
						} else {
							g_config.ffr.apply = true;
						}
					} else if (frameTime < g_config.ffr.marginFrameTime) {
						if (g_config.ffr.dynamicChangeRadius) {
							if ((g_config.ffr.innerRadius + g_config.ffr.increaseRadiusStep) <= g_config.ffr.maxRadius) {
								g_config.ffr.innerRadius += g_config.ffr.increaseRadiusStep;
								g_config.ffr.midRadius += g_config.ffr.increaseRadiusStep;
								g_config.ffr.outerRadius += g_config.ffr.increaseRadiusStep;
								g_config.ffr.radiusChanged[0] = true;
								g_config.ffr.radiusChanged[1] = true;
							}
						} else {
							g_config.ffr.apply = false;
						}
					}
				}
			}

			is_DynamicProfiling = false;
		}

		StartDynamicProfiling();
	}


/*
	void D3D11PostProcessor::CreateProfileQueries() {
		for (auto &profileQuery : profileQueries) {
			D3D11_QUERY_DESC qd;
			qd.Query = D3D11_QUERY_TIMESTAMP;
			qd.MiscFlags = 0;
			device->CreateQuery(&qd, profileQuery.queryStart.ReleaseAndGetAddressOf());
			device->CreateQuery(&qd, profileQuery.queryEnd.ReleaseAndGetAddressOf());
			qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
			device->CreateQuery(&qd, profileQuery.queryDisjoint.ReleaseAndGetAddressOf());
		}
	}

	void D3D11PostProcessor::StartProfiling() {
		if (profileQueries[0].queryStart == nullptr) {
			CreateProfileQueries();
		}

		context->Begin(profileQueries[currentQuery].queryDisjoint.Get());
		context->End(profileQueries[currentQuery].queryStart.Get());
	}

	void D3D11PostProcessor::EndProfiling() {
		context->End(profileQueries[currentQuery].queryEnd.Get());
		context->End(profileQueries[currentQuery].queryDisjoint.Get());

		currentQuery = (currentQuery + 1) % QUERY_COUNT;
		while (context->GetData(profileQueries[currentQuery].queryDisjoint.Get(), nullptr, 0, 0) == S_FALSE) {
			Sleep(1);
		}
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
		HRESULT result = context->GetData(profileQueries[currentQuery].queryDisjoint.Get(), &disjoint, sizeof(disjoint), 0);
		if (result == S_OK && !disjoint.Disjoint) {
			UINT64 begin, end;
			context->GetData(profileQueries[currentQuery].queryStart.Get(), &begin, sizeof(UINT64), 0);
			context->GetData(profileQueries[currentQuery].queryEnd.Get(), &end, sizeof(UINT64), 0);
			float duration = (end - begin) / float(disjoint.Frequency);
			summedGpuTime += duration;
			++countedQueries;

			if (countedQueries >= 500) {
				float avgTimeMs = 1000.f / countedQueries * summedGpuTime;
				// Queries are done per eye, but we want the average for both eyes per frame
				avgTimeMs *= 2;
				LOG_INFO << "Average GPU processing time for post-processing: " << avgTimeMs << " ms";
				countedQueries = 0;
				summedGpuTime = 0.f;
			}
		}
	}
*/
}
