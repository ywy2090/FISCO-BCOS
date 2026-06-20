/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief EIP-2537 BLS12-381 MSM gas (G1/G2 discount tables).
 *  @file BlsGas.h
 */
#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace bcos::evm::precompiled
{

inline constexpr std::array<uint16_t, 128> kG1MsmDiscounts = {1000, 949, 848, 797, 764, 750, 738,
    728, 719, 712, 705, 698, 692, 687, 682, 677, 673, 669, 665, 661, 658, 654, 651, 648, 645, 642,
    640, 637, 635, 632, 630, 627, 625, 623, 621, 619, 617, 615, 613, 611, 609, 608, 606, 604, 603,
    601, 599, 598, 596, 595, 593, 592, 591, 589, 588, 586, 585, 584, 582, 581, 580, 579, 577, 576,
    575, 574, 573, 572, 570, 569, 568, 567, 566, 565, 564, 563, 562, 561, 560, 559, 558, 557, 556,
    555, 554, 553, 552, 551, 550, 549, 548, 547, 547, 546, 545, 544, 543, 542, 541, 540, 540, 539,
    538, 537, 536, 536, 535, 534, 533, 532, 532, 531, 530, 529, 528, 528, 527, 526, 525, 525, 524,
    523, 522, 522, 521, 520, 520, 519};

inline constexpr std::array<uint16_t, 128> kG2MsmDiscounts = {1000, 1000, 923, 884, 855, 832, 812,
    796, 782, 770, 759, 749, 740, 732, 724, 717, 711, 704, 699, 693, 688, 683, 679, 674, 670, 666,
    663, 659, 655, 652, 649, 646, 643, 640, 637, 634, 632, 629, 627, 624, 622, 620, 618, 615, 613,
    611, 609, 607, 606, 604, 602, 600, 598, 597, 595, 593, 592, 590, 589, 587, 586, 584, 583, 582,
    580, 579, 578, 576, 575, 574, 573, 571, 570, 569, 568, 567, 566, 565, 563, 562, 561, 560, 559,
    558, 557, 556, 555, 554, 553, 552, 552, 551, 550, 549, 548, 547, 546, 545, 545, 544, 543, 542,
    541, 541, 540, 539, 538, 537, 537, 536, 535, 535, 534, 533, 532, 532, 531, 530, 530, 529, 528,
    528, 527, 526, 526, 525, 524, 524};

inline int64_t blsG1MsmGas(size_t k) noexcept
{
    if (k == 0)
    {
        return 0;
    }
    assert(k <= kG1MsmDiscounts.size() && "BLS G1MSM: too many pairs for discount table");
    auto const discount = kG1MsmDiscounts[std::min(k, kG1MsmDiscounts.size()) - 1];
    return 12000 * static_cast<int64_t>(discount) * static_cast<int64_t>(k) / 1000;
}

inline int64_t blsG2MsmGas(size_t k) noexcept
{
    if (k == 0)
    {
        return 0;
    }
    assert(k <= kG2MsmDiscounts.size() && "BLS G2MSM: too many pairs for discount table");
    auto const discount = kG2MsmDiscounts[std::min(k, kG2MsmDiscounts.size()) - 1];
    return 22500 * static_cast<int64_t>(discount) * static_cast<int64_t>(k) / 1000;
}

}  // namespace bcos::evm::precompiled
