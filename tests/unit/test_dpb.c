/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "codec/macroblock.h"
#include "dpb.h"
#include "test.h"
#include <string.h>

typedef struct test_picture {
    cavs_picture picture;
    cavs_macroblock macroblock;
    unsigned references;
} test_picture;

static void retain_test_picture(void *opaque, cavs_picture *picture) {
    test_picture *owned = (test_picture *)picture->owner;
    (void)opaque;
    ++owned->references;
}

static void release_test_picture(void *opaque, cavs_picture *picture) {
    test_picture *owned = (test_picture *)picture->owner;
    (void)opaque;
    TEST_CHECK(owned->references != 0U);
    --owned->references;
}

static void init_test_picture(test_picture *owned, cavs_picture_type type,
                              uint16_t distance) {
    memset(owned, 0, sizeof(*owned));
    owned->picture.picture_type = type;
    owned->picture.picture_distance = distance;
    owned->picture.macroblock_width = 1U;
    owned->picture.macroblock_height = 1U;
    owned->picture.macroblocks = &owned->macroblock;
    owned->picture.macroblock_count = 1U;
    owned->picture.is_reference = (uint8_t)(type != CAVS_PICTURE_B);
    owned->picture.owner = owned;
}

static void store_complete_picture(cavs_dpb *dpb, test_picture *owned) {
    TEST_CHECK(cavs_dpb_begin_picture(
                   dpb, &owned->picture, CAVS_FIELD_BOTH) == CAVS_OK);
    owned->picture.completed_fields = CAVS_FIELD_BOTH;
    owned->picture.filtered = 1U;
    TEST_CHECK(cavs_dpb_store(dpb, &owned->picture) == CAVS_OK);
}

/* GB/T 20090.16-2016 9.4.6.1: directed distances use modulo 512. */
static void test_dpb_modulo_512_distance(void) {
    cavs_dpb dpb;
    cavs_dpb_lifetime lifetime;
    cavs_dpb_reference reference;
    test_picture current;
    test_picture old;
    memset(&lifetime, 0, sizeof(lifetime));
    lifetime.retain = retain_test_picture;
    lifetime.release = release_test_picture;
    TEST_CHECK(cavs_dpb_init(&dpb) == CAVS_OK);
    TEST_CHECK(cavs_dpb_set_lifetime(&dpb, &lifetime) == CAVS_OK);
    init_test_picture(&old, CAVS_PICTURE_I, 0U);
    store_complete_picture(&dpb, &old);
    init_test_picture(&current, CAVS_PICTURE_P, 200U);
    TEST_CHECK(cavs_dpb_begin_picture(
                   &dpb, &current.picture, CAVS_FIELD_BOTH) == CAVS_OK);
    TEST_CHECK(cavs_dpb_select_reference_entry(
                   &dpb, CAVS_DPB_FORWARD, 0U, CAVS_FIELD_BOTH,
                   &reference) == CAVS_OK);
    TEST_CHECK(reference.picture == &old.picture && reference.block_distance == 400U);
    TEST_CHECK(cavs_dpb_end_picture(&dpb, &current.picture) == CAVS_OK);
    TEST_CHECK(cavs_dpb_reset(&dpb) == CAVS_OK);
    cavs_dpb_destroy(&dpb);
}

void test_dpb(void) {
    cavs_dpb dpb;
    cavs_dpb_lifetime lifetime;
    cavs_picture *output = NULL;
    test_picture i_picture;
    test_picture p_picture;
    test_picture b_picture;
    test_picture reset_picture;
    memset(&lifetime, 0, sizeof(lifetime));
    lifetime.retain = retain_test_picture;
    lifetime.release = release_test_picture;
    TEST_CHECK(cavs_dpb_init(&dpb) == CAVS_OK);
    TEST_CHECK(cavs_dpb_set_lifetime(&dpb, &lifetime) == CAVS_OK);
    init_test_picture(&i_picture, CAVS_PICTURE_I, 0U);
    init_test_picture(&p_picture, CAVS_PICTURE_P, 2U);
    init_test_picture(&b_picture, CAVS_PICTURE_B, 1U);

    store_complete_picture(&dpb, &i_picture);
    TEST_CHECK(cavs_dpb_pop_output(&dpb, &output) == CAVS_AGAIN);
    store_complete_picture(&dpb, &p_picture);
    TEST_CHECK(cavs_dpb_pop_output(&dpb, &output) == CAVS_OK);
    TEST_CHECK(output == &i_picture.picture);
    release_test_picture(NULL, output);
    store_complete_picture(&dpb, &b_picture);
    TEST_CHECK(cavs_dpb_pop_output(&dpb, &output) == CAVS_OK);
    TEST_CHECK(output == &b_picture.picture);
    release_test_picture(NULL, output);
    TEST_CHECK(cavs_dpb_flush(&dpb) == CAVS_OK);
    TEST_CHECK(cavs_dpb_pop_output(&dpb, &output) == CAVS_OK);
    TEST_CHECK(output == &p_picture.picture);
    release_test_picture(NULL, output);
    TEST_CHECK(cavs_dpb_pop_output(&dpb, &output) == CAVS_EOF);
    TEST_CHECK(i_picture.references == 0U && p_picture.references == 0U &&
               b_picture.references == 0U);

    test_dpb_modulo_512_distance();

    TEST_CHECK(cavs_dpb_reset(&dpb) == CAVS_OK);
    init_test_picture(&reset_picture, CAVS_PICTURE_I, 4U);
    store_complete_picture(&dpb, &reset_picture);
    TEST_CHECK(reset_picture.references == 2U);
    TEST_CHECK(cavs_dpb_reset(&dpb) == CAVS_OK);
    TEST_CHECK(reset_picture.references == 0U);
    cavs_dpb_destroy(&dpb);
}
