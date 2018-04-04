/*     Copyright 2015-2018 Egor Yusov
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF ANY PROPRIETARY RIGHTS.
 *
 *  In no event and under no legal theory, whether in tort (including negligence), 
 *  contract, or otherwise, unless required by applicable law (such as deliberate 
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental, 
 *  or consequential damages of any character arising as a result of this License or 
 *  out of the use or inability to use the software (including but not limited to damages 
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and 
 *  all other commercial damages or losses), even if such Contributor has been advised 
 *  of the possibility of such damages.
 */

#include "pch.h"
#include "SwapChainVkImpl.h"
#include "RenderDeviceVkImpl.h"
#include "DeviceContextVkImpl.h"
#include "VulkanTypeConversions.h"
#include "TextureVkImpl.h"
#include "EngineMemory.h"

namespace Diligent
{

SwapChainVkImpl::SwapChainVkImpl(IReferenceCounters *pRefCounters,
                                 const SwapChainDesc& SCDesc, 
                                 RenderDeviceVkImpl* pRenderDeviceVk, 
                                 DeviceContextVkImpl* pDeviceContextVk, 
                                 void* pNativeWndHandle) : 
    TSwapChainBase(pRefCounters, pRenderDeviceVk, pDeviceContextVk, SCDesc),
    m_VulkanInstance(pRenderDeviceVk->GetVulkanInstance()),
    m_pBackBufferRTV(STD_ALLOCATOR_RAW_MEM(RefCntAutoPtr<ITextureView>, GetRawAllocator(), "Allocator for vector<RefCntAutoPtr<ITextureView>>"))
{
    // Create OS-specific surface
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.hinstance = GetModuleHandle(NULL);
    surfaceCreateInfo.hwnd = (HWND)pNativeWndHandle;
    auto err = vkCreateWin32SurfaceKHR(m_VulkanInstance->GetVkInstance(), &surfaceCreateInfo, nullptr, &m_VkSurface);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.window = window;
    auto err = vkCreateAndroidSurfaceKHR(instance, &surfaceCreateInfo, NULL, &m_VkSurface);
#elif defined(VK_USE_PLATFORM_IOS_MVK)
    VkIOSSurfaceCreateInfoMVK surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_IOS_SURFACE_CREATE_INFO_MVK;
    surfaceCreateInfo.pNext = NULL;
    surfaceCreateInfo.flags = 0;
    surfaceCreateInfo.pView = view;
    auto err = vkCreateIOSSurfaceMVK(instance, &surfaceCreateInfo, nullptr, &m_VkSurface);
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
    VkMacOSSurfaceCreateInfoMVK surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK;
    surfaceCreateInfo.pNext = NULL;
    surfaceCreateInfo.flags = 0;
    surfaceCreateInfo.pView = view;
    auto err = vkCreateMacOSSurfaceMVK(instance, &surfaceCreateInfo, NULL, &m_VkSurface);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    VkWaylandSurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.display = display;
    surfaceCreatem_VkSurface = window;
    err = vkCreateWaylandSurfaceKHR(instance, &surfaceCreateInfo, nullptr, &m_VkSurface);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
    VkXcbSurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.connection = connection;
    surfaceCreateInfo.window = window;
    auto err = vkCreateXcbSurfaceKHR(instance, &surfaceCreateInfo, nullptr, &m_VkSurface);
#endif

    CHECK_VK_ERROR_AND_THROW(err, "Failed to create OS-specific surface");
    const auto& PhysicalDevice = pRenderDeviceVk->GetPhysicalDevice();
    auto *CmdQueueVK = pRenderDeviceVk->GetCmdQueue();
    auto QueueFamilyIndex = CmdQueueVK->GetQueueFamilyIndex();
    if( !PhysicalDevice.CheckPresentSupport(QueueFamilyIndex, m_VkSurface) )
    {
        LOG_ERROR_AND_THROW("Selected physical device does not support present capability.\n"
                            "There could be few ways to mitigate this problem. One is to try to find another queue that supports present, but does not support graphics and compute capabilities."
                            "Another way is to find another physical device that exposes queue family that supports present and graphics capability. Neither apporach is currently implemented in Diligent Engine.");
    }


    auto vkDeviceHandle = PhysicalDevice.GetVkDeviceHandle();
    // Get the list of VkFormats that are supported:
    uint32_t formatCount = 0;
    err = vkGetPhysicalDeviceSurfaceFormatsKHR(vkDeviceHandle, m_VkSurface, &formatCount, NULL);
    CHECK_VK_ERROR_AND_THROW(err, "Failed to query number of supported formats");
    VERIFY_EXPR(formatCount > 0);
    std::vector<VkSurfaceFormatKHR> SupportedFormats(formatCount);
    err = vkGetPhysicalDeviceSurfaceFormatsKHR(vkDeviceHandle, m_VkSurface, &formatCount, SupportedFormats.data());
    CHECK_VK_ERROR_AND_THROW(err, "Failed to query supported format properties");
    VERIFY_EXPR(formatCount == SupportedFormats.size());
    m_VkColorFormat = TexFormatToVkFormat(m_SwapChainDesc.ColorBufferFormat);
    if (formatCount == 1 && SupportedFormats[0].format == VK_FORMAT_UNDEFINED) 
    {
        // If the format list includes just one entry of VK_FORMAT_UNDEFINED,
        // the surface has no preferred format.  Otherwise, at least one
        // supported format will be returned.

        // Do nothing
    }
    else 
    {
        bool FmtFound = std::find_if(SupportedFormats.begin(), SupportedFormats.end(), 
            [&](const VkSurfaceFormatKHR &SrfFmt){return SrfFmt.format == m_VkColorFormat;}) != SupportedFormats.end();
        if(!FmtFound)
        {
            VkFormat VkReplacementColorFormat = VK_FORMAT_UNDEFINED;
            switch(m_VkColorFormat)
            {
                case VK_FORMAT_R8G8B8A8_UNORM: VkReplacementColorFormat = VK_FORMAT_B8G8R8A8_UNORM; break;
                case VK_FORMAT_B8G8R8A8_UNORM: VkReplacementColorFormat = VK_FORMAT_R8G8B8A8_UNORM; break;
                case VK_FORMAT_B8G8R8A8_SRGB: VkReplacementColorFormat = VK_FORMAT_R8G8B8A8_SRGB; break;
                case VK_FORMAT_R8G8B8A8_SRGB: VkReplacementColorFormat = VK_FORMAT_B8G8R8A8_SRGB; break;
                default: VkReplacementColorFormat = VK_FORMAT_UNDEFINED;
            }

            bool ReplacementFmtFound = std::find_if(SupportedFormats.begin(), SupportedFormats.end(),
                [&](const VkSurfaceFormatKHR &SrfFmt) {return SrfFmt.format == VkReplacementColorFormat; }) != SupportedFormats.end();
            if(ReplacementFmtFound)
            {
                m_VkColorFormat = VkReplacementColorFormat;
                auto NewColorBufferFormat = VkFormatToTexFormat(VkReplacementColorFormat);
                LOG_INFO_MESSAGE("Requested color buffer format ", GetTextureFormatAttribs(m_SwapChainDesc.ColorBufferFormat).Name, " is not supported by the surace and will be replaced with ", GetTextureFormatAttribs(NewColorBufferFormat).Name);
                m_SwapChainDesc.ColorBufferFormat = NewColorBufferFormat;
            }
            else
            {
                LOG_WARNING_MESSAGE("Requested color buffer format ", GetTextureFormatAttribs(m_SwapChainDesc.ColorBufferFormat).Name ,"is not supported by the surace");
            }
        }
    }
    
    VkSurfaceCapabilitiesKHR surfCapabilities = {};
    err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vkDeviceHandle, m_VkSurface, &surfCapabilities);
    CHECK_VK_ERROR_AND_THROW(err, "Failed to query physical device surface capabilities");

    uint32_t presentModeCount = 0;
    err = vkGetPhysicalDeviceSurfacePresentModesKHR(vkDeviceHandle, m_VkSurface, &presentModeCount, NULL);
    CHECK_VK_ERROR_AND_THROW(err, "Failed to query surface present mode count");
    VERIFY_EXPR(presentModeCount > 0);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    err = vkGetPhysicalDeviceSurfacePresentModesKHR(vkDeviceHandle, m_VkSurface, &presentModeCount, presentModes.data());
    CHECK_VK_ERROR_AND_THROW(err, "Failed to query surface present modes");
    VERIFY_EXPR(presentModeCount == presentModes.size());

    VkExtent2D swapchainExtent = {};
    // width and height are either both 0xFFFFFFFF, or both not 0xFFFFFFFF.
    if (surfCapabilities.currentExtent.width == 0xFFFFFFFF) {
        // If the surface size is undefined, the size is set to
        // the size of the images requested.
        swapchainExtent.width  = std::min(std::max(SCDesc.Width,  surfCapabilities.minImageExtent.width),  surfCapabilities.maxImageExtent.width);
        swapchainExtent.height = std::min(std::max(SCDesc.Height, surfCapabilities.minImageExtent.height), surfCapabilities.maxImageExtent.height);
    }
    else {
        // If the surface size is defined, the swap chain size must match
        swapchainExtent = surfCapabilities.currentExtent;
    }
    m_SwapChainDesc.Width  = swapchainExtent.width;
    m_SwapChainDesc.Height = swapchainExtent.height;

    // The FIFO present mode is guaranteed by the spec to be supported
    VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    bool PresentModeSupported = false;
    for(auto presentMode : presentModes)
    {
        if(presentMode == swapchainPresentMode)
        {
            PresentModeSupported = true;
            break;
        }
    }
    if(!PresentModeSupported)
        LOG_ERROR_AND_THROW("Present mode is not supported by this surface");

    // Determine the number of VkImage's to use in the swap chain.
    // We need to acquire only 1 presentable image at at time.
    // Asking for minImageCount images ensures that we can acquire
    // 1 presentable image as long as we present it before attempting
    // to acquire another.
    if(m_SwapChainDesc.BufferCount < surfCapabilities.minImageCount)
    {
        LOG_INFO_MESSAGE("Requested back buffer count (", m_SwapChainDesc.BufferCount, ") is smaller than the minimal image count supported for this surface (", surfCapabilities.minImageCount, "). Resetting to ", surfCapabilities.minImageCount);
        m_SwapChainDesc.BufferCount = surfCapabilities.minImageCount;
    }
    if (m_SwapChainDesc.BufferCount > surfCapabilities.maxImageCount)
    {
        LOG_INFO_MESSAGE("Requested back buffer count (", m_SwapChainDesc.BufferCount, ") is greater than the maximal image count supported for this surface (", surfCapabilities.maxImageCount, "). Resetting to ", surfCapabilities.maxImageCount);
        m_SwapChainDesc.BufferCount = surfCapabilities.maxImageCount;
    }
    uint32_t desiredNumberOfSwapChainImages = m_SwapChainDesc.BufferCount;

    VkSurfaceTransformFlagBitsKHR preTransform = 
        (surfCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? 
            VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : 
            surfCapabilities.currentTransform;

    // Find a supported composite alpha mode - one of these is guaranteed to be set
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    VkCompositeAlphaFlagBitsKHR compositeAlphaFlags[4] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (uint32_t i = 0; i < _countof(compositeAlphaFlags); i++) {
        if (surfCapabilities.supportedCompositeAlpha & compositeAlphaFlags[i]) {
            compositeAlpha = compositeAlphaFlags[i];
            break;
        }
    }


    VkSwapchainCreateInfoKHR swapchain_ci = {};
    swapchain_ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_ci.pNext = NULL;
    swapchain_ci.surface = m_VkSurface;
    swapchain_ci.minImageCount = desiredNumberOfSwapChainImages;
    swapchain_ci.imageFormat = m_VkColorFormat;
    swapchain_ci.imageExtent.width = swapchainExtent.width;
    swapchain_ci.imageExtent.height = swapchainExtent.height;
    swapchain_ci.preTransform = preTransform;
    swapchain_ci.compositeAlpha = compositeAlpha;
    swapchain_ci.imageArrayLayers = 1;
    swapchain_ci.presentMode = swapchainPresentMode;
    swapchain_ci.oldSwapchain = VK_NULL_HANDLE;
    swapchain_ci.clipped = true;
    swapchain_ci.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    swapchain_ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_ci.queueFamilyIndexCount = 0;
    swapchain_ci.pQueueFamilyIndices = NULL;
    //uint32_t queueFamilyIndices[] = { (uint32_t)info.graphics_queue_family_index, (uint32_t)info.present_queue_family_index };
    //if (info.graphics_queue_family_index != info.present_queue_family_index) {
    //    // If the graphics and present queues are from different queue families,
    //    // we either have to explicitly transfer ownership of images between
    //    // the queues, or we have to create the swapchain with imageSharingMode
    //    // as VK_SHARING_MODE_CONCURRENT
    //    swapchain_ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    //    swapchain_ci.queueFamilyIndexCount = 2;
    //    swapchain_ci.pQueueFamilyIndices = queueFamilyIndices;
    //}

    auto LogicalVkDevice = pRenderDeviceVk->GetVkDevice();
    err = vkCreateSwapchainKHR(LogicalVkDevice, &swapchain_ci, NULL, &m_VkSwapChain);
    CHECK_VK_ERROR_AND_THROW(err, "Failed to create Vulkan swapchain");

    uint32_t swapchainImageCount = 0;
    err = vkGetSwapchainImagesKHR(LogicalVkDevice, m_VkSwapChain, &swapchainImageCount, NULL);
    CHECK_VK_ERROR_AND_THROW(err, "Failed to request swap chain image count");
    VERIFY_EXPR(swapchainImageCount > 0);
    if (swapchainImageCount != m_SwapChainDesc.BufferCount)
    {
        LOG_INFO_MESSAGE("Actual number of images in the created swap chain: ", m_SwapChainDesc.BufferCount);
        m_SwapChainDesc.BufferCount = swapchainImageCount;
    }

    InitBuffersAndViews();
}

SwapChainVkImpl::~SwapChainVkImpl()
{
    if(m_VkSwapChain != VK_NULL_HANDLE)
    {
        auto *pDeviceVkImpl = ValidatedCast<RenderDeviceVkImpl>(m_pRenderDevice.RawPtr());
        vkDestroySwapchainKHR(pDeviceVkImpl->GetVkDevice(), m_VkSwapChain, NULL);
    }
}

void SwapChainVkImpl::InitBuffersAndViews()
{
    auto *pDeviceVkImpl = ValidatedCast<RenderDeviceVkImpl>(m_pRenderDevice.RawPtr());
    auto LogicalVkDevice = pDeviceVkImpl->GetVkDevice();

#ifdef _DEBUG
    {
        uint32_t swapchainImageCount = 0;
        auto err = vkGetSwapchainImagesKHR(LogicalVkDevice, m_VkSwapChain, &swapchainImageCount, NULL);
        VERIFY_EXPR(err == VK_SUCCESS);
        VERIFY(swapchainImageCount == m_SwapChainDesc.BufferCount, "Unexpected swap chain buffer count");
    }
#endif

    m_pBackBufferRTV.resize(m_SwapChainDesc.BufferCount);

    uint32_t swapchainImageCount = m_SwapChainDesc.BufferCount;
    std::vector<VkImage> swapchainImages(swapchainImageCount);
    auto err = vkGetSwapchainImagesKHR(LogicalVkDevice, m_VkSwapChain, &swapchainImageCount, swapchainImages.data());
    CHECK_VK_ERROR_AND_THROW(err, "Failed to get swap chain images");
    VERIFY_EXPR(swapchainImageCount == swapchainImages.size());

    for (uint32_t i = 0; i < swapchainImageCount; i++) {

        TextureDesc BackBufferDesc;
        BackBufferDesc.Format = m_SwapChainDesc.ColorBufferFormat;
        std::stringstream name_ss;
        name_ss << "Main back buffer " << i;
        auto name = name_ss.str();
        BackBufferDesc.Name = name.c_str();
        BackBufferDesc.Type = RESOURCE_DIM_TEX_2D;
        BackBufferDesc.Width  = m_SwapChainDesc.Width;
        BackBufferDesc.Height = m_SwapChainDesc.Height;
        BackBufferDesc.Format = m_SwapChainDesc.ColorBufferFormat;
        BackBufferDesc.MipLevels = 1;

        RefCntAutoPtr<TextureVkImpl> pBackBufferTex;
        ValidatedCast<RenderDeviceVkImpl>(m_pRenderDevice.RawPtr())->CreateTexture(BackBufferDesc, swapchainImages[i], &pBackBufferTex);
        
        UNSUPPORTED("TODO: move all this code to Texture creation");
        VkImageViewCreateInfo color_image_view = {};
        color_image_view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        color_image_view.pNext = NULL;
        color_image_view.flags = 0;
        color_image_view.image = swapchainImages[i];
        color_image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        color_image_view.format = m_VkColorFormat;
        color_image_view.components.r = VK_COMPONENT_SWIZZLE_R;
        color_image_view.components.g = VK_COMPONENT_SWIZZLE_G;
        color_image_view.components.b = VK_COMPONENT_SWIZZLE_B;
        color_image_view.components.a = VK_COMPONENT_SWIZZLE_A;
        color_image_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        color_image_view.subresourceRange.baseMipLevel = 0;
        color_image_view.subresourceRange.levelCount = 1;
        color_image_view.subresourceRange.baseArrayLayer = 0;
        color_image_view.subresourceRange.layerCount = 1;

        VkImageView vkImgView = VK_NULL_HANDLE;
        err = vkCreateImageView(LogicalVkDevice, &color_image_view, NULL, &vkImgView);
        CHECK_VK_ERROR_AND_THROW(err, "Failed to create view for a swap chain image");

        TextureViewDesc RTVDesc;
        RTVDesc.ViewType = TEXTURE_VIEW_RENDER_TARGET;
        RefCntAutoPtr<ITextureView> pRTV;
        pBackBufferTex->CreateView(RTVDesc, &pRTV);
        m_pBackBufferRTV[i] = RefCntAutoPtr<ITextureViewVk>(pRTV, IID_TextureViewVk);
    }

#if 0
    /* VULKAN_KEY_END */

    /* Clean Up */
    for (uint32_t i = 0; i < info.swapchainImageCount; i++) {
        vkDestroyImageView(LogicalVkDevice, info.buffers[i].view, NULL);
    }
#endif

#if 0
    
    for(Uint32 backbuff = 0; backbuff < m_SwapChainDesc.BufferCount; ++backbuff)
    {
		CComPtr<IVkResource> pBackBuffer;
        auto hr = m_pSwapChain->GetBuffer(backbuff, __uuidof(pBackBuffer), reinterpret_cast<void**>( static_cast<IVkResource**>(&pBackBuffer) ));
        if(FAILED(hr))
            LOG_ERROR_AND_THROW("Failed to get back buffer ", backbuff," from the swap chain");

        hr = pBackBuffer->SetName(L"Main back buffer");
        VERIFY_EXPR(SUCCEEDED(hr));

    }

    TextureDesc DepthBufferDesc;
    DepthBufferDesc.Type = RESOURCE_DIM_TEX_2D;
    DepthBufferDesc.Width = m_SwapChainDesc.Width;
    DepthBufferDesc.Height = m_SwapChainDesc.Height;
    DepthBufferDesc.Format = m_SwapChainDesc.DepthBufferFormat;
    DepthBufferDesc.SampleCount = m_SwapChainDesc.SamplesCount;
    DepthBufferDesc.Usage = USAGE_DEFAULT;
    DepthBufferDesc.BindFlags = BIND_DEPTH_STENCIL;

    DepthBufferDesc.ClearValue.Format = DepthBufferDesc.Format;
    DepthBufferDesc.ClearValue.DepthStencil.Depth = m_SwapChainDesc.DefaultDepthValue;
    DepthBufferDesc.ClearValue.DepthStencil.Stencil = m_SwapChainDesc.DefaultStencilValue;
    DepthBufferDesc.Name = "Main depth buffer";
    RefCntAutoPtr<ITexture> pDepthBufferTex;
    m_pRenderDevice->CreateTexture(DepthBufferDesc, TextureData(), static_cast<ITexture**>(&pDepthBufferTex) );
    auto pDSV = pDepthBufferTex->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    m_pDepthBufferDSV = RefCntAutoPtr<ITextureViewVk>(pDSV, IID_TextureViewVk);
#endif
}

IMPLEMENT_QUERY_INTERFACE( SwapChainVkImpl, IID_SwapChainVk, TSwapChainBase )


void SwapChainVkImpl::Present(Uint32 SyncInterval)
{
#if 0
    UINT SyncInterval = 0;
#if PLATFORM_UNIVERSAL_WINDOWS
    SyncInterval = 1; // Interval 0 is not supported on Windows Phone 
#endif

    auto pDeviceContext = m_wpDeviceContext.Lock();
    if( !pDeviceContext )
    {
        LOG_ERROR_MESSAGE( "Immediate context has been released" );
        return;
    }

    auto *pImmediateCtx = pDeviceContext.RawPtr();
    auto *pImmediateCtxVk = ValidatedCast<DeviceContextVkImpl>( pImmediateCtx );

    auto *pCmdCtx = pImmediateCtxVk->RequestCmdContext();
    auto *pBackBuffer = ValidatedCast<TextureVkImpl>( GetCurrentBackBufferRTV()->GetTexture() );
    pCmdCtx->TransitionResource( pBackBuffer, Vk_RESOURCE_STATE_PRESENT);

    pImmediateCtxVk->Flush();

    auto *pDeviceVk = ValidatedCast<RenderDeviceVkImpl>( pImmediateCtxVk->GetDevice() );
    
    auto hr = m_pSwapChain->Present( SyncInterval, 0 );
    VERIFY(SUCCEEDED(hr), "Present failed");

    pDeviceVk->FinishFrame();

#if 0
#if PLATFORM_UNIVERSAL_WINDOWS
    // A successful Present call for DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL SwapChains unbinds 
    // backbuffer 0 from all GPU writeable bind points.
    // We need to rebind all render targets to make sure that
    // the back buffer is not unbound
    pImmediateCtxVk->CommitRenderTargets();
#endif
#endif
#endif
}

void SwapChainVkImpl::Resize( Uint32 NewWidth, Uint32 NewHeight )
{
    if( TSwapChainBase::Resize(NewWidth, NewHeight) )
    {
#if 0
        auto pDeviceContext = m_wpDeviceContext.Lock();
        VERIFY( pDeviceContext, "Immediate context has been released" );
        if( pDeviceContext )
        {
            RenderDeviceVkImpl *pDeviceVk = ValidatedCast<RenderDeviceVkImpl>(m_pRenderDevice.RawPtr());
            pDeviceContext->Flush();

            try
            {
                auto *pImmediateCtxVk = ValidatedCast<DeviceContextVkImpl>(pDeviceContext.RawPtr());
                bool bIsDefaultFBBound = pImmediateCtxVk->IsDefaultFBBound();

                // All references to the swap chain must be released before it can be resized
                m_pBackBufferRTV.clear();
                m_pDepthBufferDSV.Release();

                // This will release references to Vk swap chain buffers hold by
                // m_pBackBufferRTV[]
                pDeviceVk->IdleGPU(true);

                DXGI_SWAP_CHAIN_DESC SCDes;
                memset( &SCDes, 0, sizeof( SCDes ) );
                m_pSwapChain->GetDesc( &SCDes );
                CHECK_D3D_RESULT_THROW( m_pSwapChain->ResizeBuffers(SCDes.BufferCount, m_SwapChainDesc.Width, 
                                                                    m_SwapChainDesc.Height, SCDes.BufferDesc.Format, 
                                                                    SCDes.Flags),
                                        "Failed to resize the DXGI swap chain" );


                InitBuffersAndViews();
                
                if( bIsDefaultFBBound )
                {
                    // Set default render target and viewport
                    pDeviceContext->SetRenderTargets( 0, nullptr, nullptr );
                    pDeviceContext->SetViewports( 1, nullptr, 0, 0 );
                }
            }
            catch( const std::runtime_error & )
            {
                LOG_ERROR( "Failed to resize the swap chain" );
            }
        }
#endif
    }
}

#if 0
ITextureViewVk *SwapChainVkImpl::GetCurrentBackBufferRTV()
{
    auto CurrentBackBufferIndex = m_pSwapChain->GetCurrentBackBufferIndex();
    VERIFY_EXPR(CurrentBackBufferIndex >= 0 && CurrentBackBufferIndex < m_SwapChainDesc.BufferCount);
    return m_pBackBufferRTV[CurrentBackBufferIndex];
}
#endif

void SwapChainVkImpl::SetFullscreenMode(const DisplayModeAttribs &DisplayMode)
{
}

void SwapChainVkImpl::SetWindowedMode()
{
}


}