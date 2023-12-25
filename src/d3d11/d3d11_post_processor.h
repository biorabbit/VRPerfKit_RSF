#pragma once
#include "types.h"
#include "d3d11_helper.h"
#include "d3d11_injector.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "openvr.h"

namespace vrperfkit {
	struct D3D11PostProcessInput {
		ID3D11Texture2D *inputTexture;
		ID3D11Texture2D *outputTexture;
		ID3D11ShaderResourceView *inputView;
		ID3D11ShaderResourceView *outputView;
		ID3D11UnorderedAccessView *outputUav;
		Viewport inputViewport;
		int eye;
		TextureMode mode;
		Point<float> projectionCenter;
	};

	class D3D11Upscaler {
	public:
		virtual void Upscale(const D3D11PostProcessInput &input, const Viewport &outputViewport) = 0;
	};

	class D3D11PostProcessor : public D3D11Listener {
	public:
		D3D11PostProcessor(ComPtr<ID3D11Device> device);

		HRESULT ClearDepthStencilView(ID3D11DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil);

		bool Apply(const D3D11PostProcessInput &input, Viewport &outputViewport);

		bool PrePSSetSamplers(UINT startSlot, UINT numSamplers, ID3D11SamplerState * const *ppSamplers) override;

		void D3D11PostProcessor::SetProjCenters(float LX, float LY, float RX, float RY);

	private:
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		std::unique_ptr<D3D11Upscaler> upscaler;
		UpscaleMethod upscaleMethod;

		void PrepareUpscaler(ID3D11Texture2D *outputTexture);

		std::unordered_set<ID3D11SamplerState*> passThroughSamplers;
		std::unordered_map<ID3D11SamplerState*, ComPtr<ID3D11SamplerState>> mappedSamplers;
		float mipLodBias = 0.0f;


		struct DynamicProfileQuery {
			ComPtr<ID3D11Query> queryDisjoint;
			ComPtr<ID3D11Query> queryStart;
			ComPtr<ID3D11Query> queryEnd;
		};
		static const int DYNAMIC_QUERY_COUNT = 1;
		int DynamicSleepCount = 0;
		DynamicProfileQuery dynamicProfileQueries[DYNAMIC_QUERY_COUNT];
		int DynamicCurrentQuery = 0;
		float DynamicSummedGpuTime = 0.0f;
		int DynamicCountedQueries = 0;
		bool is_DynamicProfiling = false;
		bool enableDynamic = false;
		bool hiddenMaskApply = false;
		bool is_rdm = false;
		bool preciseResolution = false;
		int ignoreFirstTargetRenders = 0;
		int ignoreLastTargetRenders = 0;
		int renderOnlyTarget = 0;

		void CreateDynamicProfileQueries();
		void StartDynamicProfiling();
		void EndDynamicProfiling();

		ComPtr<ID3D11Texture2D> copiedTexture;
		ComPtr<ID3D11ShaderResourceView> copiedTextureView;
		ComPtr<ID3D11SamplerState> sampler;
		bool hrmInitialized = false;
		uint32_t textureWidth = 0;
		uint32_t textureHeight = 0;
		bool requiresCopy = false;
		bool inputIsSrgb = false;
		ComPtr<ID3D11VertexShader> hrmFullTriVertexShader;
		ComPtr<ID3D11PixelShader> hrmMaskingShader;
		ComPtr<ID3D11PixelShader> rdmMaskingShader;
		ComPtr<ID3D11ComputeShader> rdmReconstructShader;
		ComPtr<ID3D11Buffer> hrmMaskingConstantsBuffer[2];
		ComPtr<ID3D11Buffer> rdmReconstructConstantsBuffer[2];
		ComPtr<ID3D11Texture2D> rdmReconstructedTexture;
		ComPtr<ID3D11UnorderedAccessView> rdmReconstructedUav;
		ComPtr<ID3D11ShaderResourceView> rdmReconstructedView;
		ComPtr<ID3D11DepthStencilState> hrmDepthStencilState;
		ComPtr<ID3D11RasterizerState> hrmRasterizerState;
		float projX[2];
		float projY[2];
		int depthClearCount = 0;
		int depthClearCountMax = 0;
		float edgeRadius = 1.15f;
		
		struct DepthStencilViews {
			ComPtr<ID3D11DepthStencilView> view[2];
		};
		std::unordered_map<ID3D11Texture2D*, DepthStencilViews> depthStencilViews;

		bool D3D11PostProcessor::HasBlacklistedTextureName(ID3D11Texture2D *tex);
		ID3D11DepthStencilView * D3D11PostProcessor::GetDepthStencilView(ID3D11Texture2D *depthStencilTex, vr::EVREye eye);
		void D3D11PostProcessor::PrepareResources(ID3D11Texture2D *inputTexture);
		void D3D11PostProcessor::PrepareCopyResources(DXGI_FORMAT format);
		void D3D11PostProcessor::PrepareRdmResources(DXGI_FORMAT format);
		void D3D11PostProcessor::ApplyRadialDensityMask(ID3D11Texture2D *depthStencilTex, float depth, uint8_t stencil);
		void D3D11PostProcessor::ReconstructRdmRender(const D3D11PostProcessInput &input);

/*
		struct ProfileQuery {
			ComPtr<ID3D11Query> queryDisjoint;
			ComPtr<ID3D11Query> queryStart;
			ComPtr<ID3D11Query> queryEnd;
		};
		static const int QUERY_COUNT = 6;
		ProfileQuery profileQueries[QUERY_COUNT];
		int currentQuery = 0;
		float summedGpuTime = 0.0f;
		int countedQueries = 0;

		void CreateProfileQueries();
		void StartProfiling();
		void EndProfiling();
*/
	};
}
