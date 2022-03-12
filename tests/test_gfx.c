#include <malloc.h>
#include <rspq_constants.h>
#include "../src/gfx/gfx_internal.h"

static volatile int dp_intr_raised;

const unsigned long gfx_timeout = 100;

void dp_interrupt_handler()
{
    dp_intr_raised = 1;
}

void wait_for_dp_interrupt(unsigned long timeout)
{
    unsigned long time_start = get_ticks_ms();

    while (get_ticks_ms() - time_start < timeout) {
        // Wait until the interrupt was raised
        if (dp_intr_raised) {
            break;
        }
    }
}

void test_gfx_rdp_interrupt(TestContext *ctx)
{
    dp_intr_raised = 0;
    register_DP_handler(dp_interrupt_handler);
    DEFER(unregister_DP_handler(dp_interrupt_handler));
    set_DP_interrupt(1);
    DEFER(set_DP_interrupt(0));

    rspq_init();
    DEFER(rspq_close());
    gfx_init();
    DEFER(gfx_close());

    rdp_sync_full_raw();
    rspq_rdp_flush();
    rspq_flush();

    wait_for_dp_interrupt(gfx_timeout);

    ASSERT(dp_intr_raised, "Interrupt was not raised!");
}

void test_gfx_dram_buffer(TestContext *ctx)
{
    dp_intr_raised = 0;
    register_DP_handler(dp_interrupt_handler);
    DEFER(unregister_DP_handler(dp_interrupt_handler));
    set_DP_interrupt(1);
    DEFER(set_DP_interrupt(0));

    rspq_init();
    DEFER(rspq_close());
    gfx_init();
    DEFER(gfx_close());

    extern void *rspq_rdp_dynamic_buffer;
    extern void *rspq_rdp_buffers[2];

    const uint32_t fbsize = 32 * 32 * 2;
    void *framebuffer = memalign(64, fbsize);
    DEFER(free(framebuffer));
    memset(framebuffer, 0, fbsize);

    data_cache_hit_writeback_invalidate(framebuffer, fbsize);

    rdp_set_other_modes_raw(SOM_CYCLE_FILL);
    rdp_set_scissor_raw(0, 0, 32 << 2, 32 << 2);
    rdp_set_fill_color_raw(0xFFFFFFFF);
    rspq_noop();
    rdp_set_color_image_raw((uint32_t)framebuffer, RDP_TILE_FORMAT_RGBA, RDP_TILE_SIZE_16BIT, 31);
    rdp_fill_rectangle_raw(0, 0, 32 << 2, 32 << 2);
    rdp_sync_full_raw();
    rspq_rdp_flush();
    rspq_flush();

    wait_for_dp_interrupt(gfx_timeout);

    ASSERT(dp_intr_raised, "Interrupt was not raised!");

    uint64_t expected_data_dynamic[] = {
        (0x2FULL << 56) | SOM_CYCLE_FILL
    };

    uint64_t expected_data_static[] = {
        (0x2DULL << 56) | (32ULL << 14) | (32ULL << 2),
        (0x37ULL << 56) | 0xFFFFFFFFULL,
        (0x3FULL << 56) | ((uint64_t)RDP_TILE_FORMAT_RGBA << 53) | ((uint64_t)RDP_TILE_SIZE_16BIT << 51) | (31ULL << 32) | ((uint32_t)framebuffer & 0x1FFFFFF),
        (0x36ULL << 56) | (32ULL << 46) | (32ULL << 34),
        0x29ULL << 56
    };

    ASSERT_EQUAL_MEM((uint8_t*)rspq_rdp_dynamic_buffer, (uint8_t*)expected_data_dynamic, sizeof(expected_data_dynamic), "Unexpected data in dynamic DRAM buffer!");
    ASSERT_EQUAL_MEM((uint8_t*)rspq_rdp_buffers[0], (uint8_t*)expected_data_static, sizeof(expected_data_static), "Unexpected data in static DRAM buffer!");

    for (uint32_t i = 0; i < 32 * 32; i++)
    {
        ASSERT_EQUAL_HEX(UncachedUShortAddr(framebuffer)[i], 0xFFFF, "Framebuffer was not cleared properly! Index: %lu", i);
    }
}

void test_gfx_static(TestContext *ctx)
{
    dp_intr_raised = 0;
    register_DP_handler(dp_interrupt_handler);
    DEFER(unregister_DP_handler(dp_interrupt_handler));
    set_DP_interrupt(1);
    DEFER(set_DP_interrupt(0));

    rspq_init();
    DEFER(rspq_close());
    gfx_init();
    DEFER(gfx_close());

    #define TEST_GFX_FBWIDTH 64
    #define TEST_GFX_FBAREA  TEST_GFX_FBWIDTH * TEST_GFX_FBWIDTH
    #define TEST_GFX_FBSIZE  TEST_GFX_FBAREA * 2

    void *framebuffer = memalign(64, TEST_GFX_FBSIZE);
    DEFER(free(framebuffer));
    data_cache_hit_invalidate(framebuffer, TEST_GFX_FBSIZE);
    memset(framebuffer, 0, TEST_GFX_FBSIZE);

    static uint16_t expected_fb[TEST_GFX_FBAREA];
    memset(expected_fb, 0, sizeof(expected_fb));

    rdp_set_other_modes_raw(SOM_CYCLE_FILL | SOM_ATOMIC_PRIM);
    rdp_set_color_image_raw((uint32_t)framebuffer, RDP_TILE_FORMAT_RGBA, RDP_TILE_SIZE_16BIT, TEST_GFX_FBWIDTH - 1);

    uint32_t color = 0;

    for (uint32_t y = 0; y < TEST_GFX_FBWIDTH; y++)
    {
        for (uint32_t x = 0; x < TEST_GFX_FBWIDTH; x += 4)
        {
            expected_fb[y * TEST_GFX_FBWIDTH + x] = (uint16_t)color;
            expected_fb[y * TEST_GFX_FBWIDTH + x + 1] = (uint16_t)color;
            expected_fb[y * TEST_GFX_FBWIDTH + x + 2] = (uint16_t)color;
            expected_fb[y * TEST_GFX_FBWIDTH + x + 3] = (uint16_t)color;
            rdp_sync_pipe_raw();
            rdp_set_fill_color_raw(color | (color << 16));
            rdp_set_scissor_raw(x << 2, y << 2, (x + 4) << 2, (y + 1) << 2);
            rdp_fill_rectangle_raw(0, 0, TEST_GFX_FBWIDTH << 2, TEST_GFX_FBWIDTH << 2);
            color += 8;
        }
    }

    rdp_sync_full_raw();
    rspq_rdp_flush();
    rspq_flush();

    wait_for_dp_interrupt(gfx_timeout);

    ASSERT(dp_intr_raised, "Interrupt was not raised!");
    
    //dump_mem(framebuffer, TEST_GFX_FBSIZE);
    //dump_mem(expected_fb, TEST_GFX_FBSIZE);

    ASSERT_EQUAL_MEM((uint8_t*)framebuffer, (uint8_t*)expected_fb, TEST_GFX_FBSIZE, "Framebuffer contains wrong data!");

    #undef TEST_GFX_FBWIDTH
    #undef TEST_GFX_FBAREA
    #undef TEST_GFX_FBSIZE
}

void test_gfx_mixed(TestContext *ctx)
{
    dp_intr_raised = 0;
    register_DP_handler(dp_interrupt_handler);
    DEFER(unregister_DP_handler(dp_interrupt_handler));
    set_DP_interrupt(1);
    DEFER(set_DP_interrupt(0));

    rspq_init();
    DEFER(rspq_close());
    gfx_init();
    DEFER(gfx_close());
    test_ovl_init();
    DEFER(test_ovl_close());

    #define TEST_GFX_FBWIDTH 64
    #define TEST_GFX_FBAREA  TEST_GFX_FBWIDTH * TEST_GFX_FBWIDTH
    #define TEST_GFX_FBSIZE  TEST_GFX_FBAREA * 2

    void *framebuffer = memalign(64, TEST_GFX_FBSIZE);
    DEFER(free(framebuffer));
    data_cache_hit_invalidate(framebuffer, TEST_GFX_FBSIZE);
    memset(framebuffer, 0, TEST_GFX_FBSIZE);

    void *texture = malloc_uncached(TEST_GFX_FBWIDTH * 2);
    DEFER(free_uncached(texture));
    for (uint16_t i = 0; i < TEST_GFX_FBWIDTH; i++)
    {
        ((uint16_t*)texture)[i] = 0xFFFF - i;
    }
    

    static uint16_t expected_fb[TEST_GFX_FBAREA];
    memset(expected_fb, 0, sizeof(expected_fb));

    rdp_set_color_image_raw((uint32_t)framebuffer, RDP_TILE_FORMAT_RGBA, RDP_TILE_SIZE_16BIT, TEST_GFX_FBWIDTH - 1);

    uint32_t color = 0;

    for (uint32_t y = 0; y < TEST_GFX_FBWIDTH; y++)
    {
        rdp_set_other_modes_raw(SOM_CYCLE_FILL | SOM_ATOMIC_PRIM);

        uint32_t dyn_count = RANDN(0x80);
        for (uint32_t i = 0; i < dyn_count; i++)
        {
            rspq_test_send_rdp(0);
        }
        
        for (uint32_t x = 0; x < TEST_GFX_FBWIDTH; x += 4)
        {
            expected_fb[y * TEST_GFX_FBWIDTH + x + 0] = (uint16_t)color;
            expected_fb[y * TEST_GFX_FBWIDTH + x + 1] = (uint16_t)color;
            expected_fb[y * TEST_GFX_FBWIDTH + x + 2] = (uint16_t)color;
            expected_fb[y * TEST_GFX_FBWIDTH + x + 3] = (uint16_t)color;
            rdp_set_fill_color_raw(color | (color << 16));
            rdp_set_scissor_raw(x << 2, y << 2, (x + 4) << 2, (y + 1) << 2);
            rdp_fill_rectangle_raw(0, 0, TEST_GFX_FBWIDTH << 2, TEST_GFX_FBWIDTH << 2);
            rdp_sync_pipe_raw();
            color += 8;
        }

        ++y;

        dyn_count = RANDN(0x80);
        for (uint32_t i = 0; i < dyn_count; i++)
        {
            rspq_test_send_rdp(0);
        }

        rdp_set_other_modes_raw(SOM_CYCLE_COPY | SOM_ATOMIC_PRIM);
        rdp_set_texture_image_raw((uint32_t)texture, RDP_TILE_FORMAT_RGBA, RDP_TILE_SIZE_16BIT, TEST_GFX_FBWIDTH - 1);
        rdp_set_tile_raw(
            RDP_TILE_FORMAT_RGBA, 
            RDP_TILE_SIZE_16BIT,
            16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        rdp_load_tile_raw(0, 0, 0, TEST_GFX_FBWIDTH << 2, 1 << 2);
        for (uint32_t x = 0; x < TEST_GFX_FBWIDTH; x += 4)
        {
            expected_fb[y * TEST_GFX_FBWIDTH + x + 0] = (uint16_t)(0xFFFF - (x + 0));
            expected_fb[y * TEST_GFX_FBWIDTH + x + 1] = (uint16_t)(0xFFFF - (x + 1));
            expected_fb[y * TEST_GFX_FBWIDTH + x + 2] = (uint16_t)(0xFFFF - (x + 2));
            expected_fb[y * TEST_GFX_FBWIDTH + x + 3] = (uint16_t)(0xFFFF - (x + 3));
            rdp_set_scissor_raw(x << 2, y << 2, (x + 4) << 2, (y + 1) << 2);
            rdp_texture_rectangle_raw(0, 
                x << 2, y << 2, (x + 4) << 2, (y + 1) << 2,
                x << 5, 0, 4 << 10, 1 << 10);
            rdp_sync_pipe_raw();
        }
    }

    rdp_sync_full_raw();
    rspq_rdp_flush();
    rspq_flush();

    wait_for_dp_interrupt(gfx_timeout);

    ASSERT(dp_intr_raised, "Interrupt was not raised!");
    
    //dump_mem(framebuffer, TEST_GFX_FBSIZE);
    //dump_mem(expected_fb, TEST_GFX_FBSIZE);

    ASSERT_EQUAL_MEM((uint8_t*)framebuffer, (uint8_t*)expected_fb, TEST_GFX_FBSIZE, "Framebuffer contains wrong data!");

    #undef TEST_GFX_FBWIDTH
    #undef TEST_GFX_FBAREA
    #undef TEST_GFX_FBSIZE
}