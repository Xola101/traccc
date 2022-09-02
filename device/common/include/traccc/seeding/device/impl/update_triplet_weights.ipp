/** TRACCC library, part of the ACTS project (R&D line)
 *
 * (c) 2021-2022 CERN for the benefit of the ACTS project
 *
 * Mozilla Public License Version 2.0
 */

// System include(s).
#include <cassert>

#pragma once

namespace traccc::device {

TRACCC_HOST_DEVICE
void update_triplet_weights(
    const std::size_t globalIndex, const seedfilter_config& filter_config,
    const sp_grid_const_view& sp_view,
    const vecmem::data::vector_view<const prefix_sum_element_t>&
        triplet_ps_view,
    triplet_container_view triplet_view) {

    // Check if anything needs to be done.
    const vecmem::device_vector<const prefix_sum_element_t> triplet_prefix_sum(
        triplet_ps_view);
    if (globalIndex >= triplet_prefix_sum.size()) {
        return;
    }

    // Set up the device containers
    const const_sp_grid_device sp_grid(sp_view);

    const prefix_sum_element_t ps_idx = triplet_prefix_sum[globalIndex];
    const unsigned int bin_idx = ps_idx.first;

    device_triplet_container triplets(triplet_view);
    vecmem::device_vector<triplet> triplets_per_bin =
        triplets.get_items().at(bin_idx);

    // Current work item
    triplet this_triplet = triplets_per_bin[ps_idx.second];

    const sp_location& spT_idx = this_triplet.sp3;

    const traccc::internal_spacepoint<traccc::spacepoint> current_spT =
        sp_grid.bin(spT_idx.bin_idx)[spT_idx.sp_idx];

    const scalar currentTop_r = current_spT.radius();

    // if two compatible seeds with high distance in r are found, compatible
    // seeds span 5 layers
    // -> very good seed
    const scalar lowerLimitCurv =
        this_triplet.curvature - filter_config.deltaInvHelixDiameter;
    const scalar upperLimitCurv =
        this_triplet.curvature + filter_config.deltaInvHelixDiameter;
    std::size_t num_compat_seedR = 0;

    static constexpr std::size_t MAX_COMPAT_SEED = 5;
    assert(MAX_COMPAT_SEED >= filter_config.compatSeedLimit);
    scalar compat_seedR[MAX_COMPAT_SEED];

    // iterate over triplets
    for (auto tr_it = triplets_per_bin.begin(); tr_it != triplets_per_bin.end();
         tr_it++) {
        // only use other triplets which share same midBot doublet
        if (this_triplet == *tr_it || !(this_triplet.sp1 == tr_it->sp1) ||
            !(this_triplet.sp2 == tr_it->sp2)) {
            continue;
        }

        const triplet& other_triplet = *tr_it;
        const sp_location other_spT_idx = other_triplet.sp3;
        const traccc::internal_spacepoint<traccc::spacepoint> other_spT =
            sp_grid.bin(other_spT_idx.bin_idx)[other_spT_idx.sp_idx];

        // compared top SP should have at least deltaRMin distance
        const scalar otherTop_r = other_spT.radius();
        const scalar deltaR = currentTop_r - otherTop_r;
        if (std::abs(deltaR) < filter_config.deltaRMin) {
            continue;
        }

        // curvature difference within limits?
        // TODO: how much slower than sorting all vectors by curvature
        // and breaking out of loop? i.e. is vector size large (e.g. in
        // jets?)
        if (other_triplet.curvature < lowerLimitCurv) {
            continue;
        }
        if (other_triplet.curvature > upperLimitCurv) {
            continue;
        }

        bool newCompSeed = true;

        for (std::size_t i_s = 0; i_s < num_compat_seedR; ++i_s) {
            const scalar previousDiameter = compat_seedR[i_s];

            // original ATLAS code uses higher min distance for 2nd found
            // compatible seed (20mm instead of 5mm) add new compatible seed
            // only if distance larger than rmin to all other compatible
            // seeds
            if (std::abs(previousDiameter - otherTop_r) <
                filter_config.deltaRMin) {
                newCompSeed = false;
                break;
            }
        }

        if (newCompSeed) {
            compat_seedR[num_compat_seedR] = otherTop_r;
            this_triplet.weight += filter_config.compatSeedWeight;
            num_compat_seedR++;
        }

        if (num_compat_seedR >= filter_config.compatSeedLimit) {
            break;
        }
    }

    triplets_per_bin[ps_idx.second].weight = this_triplet.weight;
}

}  // namespace traccc::device