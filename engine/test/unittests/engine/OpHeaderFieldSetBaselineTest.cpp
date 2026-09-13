// OpHeaderFieldSetBaselineTest.cpp — per-fork header RLP field-set baseline (Plan C / WI-13).
//
// Two independent halves over the whole golden corpus:
//
//   (a) BYTE-EQUIVALENCE (decisive): decode each golden's encodedHeaderHex into the
//       RLP-domain EthBlockHeader and re-encode it — the bytes must come back identical.
//       This fails on ANY divergence in optional-field presence, order or value, which
//       is exactly what "the field set changed at fork X" looks like from outside. It
//       also subsumes the slotNumber requirement: a golden carrying a 22nd tail slot
//       cannot round-trip through this repo's 21-field model (EthBlockHeaderData has
//       exactly six std::optional tails).
//
//   (b) PRESENCE INVARIANTS (readable): read those six optionals off the decoded header
//       and assert (i) uniformity within a fork, (ii) monotonicity along the fork order,
//       (iii) the documented boundary facts. The observed table is printed for the record.
//
// Staying in the RLP domain is deliberate: BlockHeaderImpl (what toTarsHeader produces)
// PROJECTS the optionals — FISCO's op header carries present-zero sentinels — so going
// through it would hide absence and make this test vacuous.
//
// FISCO codec semantics (verified, bcos-codec/rlp/RLPDecode.h:307-320): optional presence
// is decided by "the input view is exhausted", so geth's middle 0x80 slot decodes to a
// PRESENT zero (u256 0) — presence here matches geth's slot shape exactly. The encoder
// (RLPEncode.h:131-137) OMITS a nullopt entirely (no 0x80 placeholder): re-encoding the
// goldens cannot diverge because geth bytes always decode to a contiguous present prefix,
// but a genuine middle-nullopt would round-trip differently — the invariant checks below
// pin that prefix shape per fork.
//
// Oracle: op-geth e8800cffe core/types/block.go (Header = 15 required + 7 optional tail
// slots, order baseFeePerGas, withdrawalsRoot, blobGasUsed, excessBlobGas,
// parentBeaconBlockRoot, requestsHash, slotNumber) and core/types/gen_header_rlp.go
// (a tail slot is emitted iff itself or any LATER slot is non-nil; a nil middle slot is
// written as 0x80), plus this repo's rebuildOpEthHeader gating
// (engine/bcos-engine/OpEngineService.cpp:406-470: >= Canyon withdrawalsRoot, >= Ecotone
// blob pair + beacon root, >= Isthmus requestsHash, Jovian/Karst add nothing).
//
// Karst has NO golden in the corpus (regolith..jovian only): its row is asserted by
// construction and printed as such — never presented as measured.

#include <bcos-rlp-protocol/EthBlockHeader.h>   // EthBlockHeader::rlpDecode/rlpEncode, data()
#include <bcos-utilities/DataConvertUtility.h>  // bcos::fromHex, bcos::ref
#include <json/json.h>
#include <boost/test/tree/decorator.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace
{

// Canonical fork order of the corpus (regolith..jovian; karst has no golden).
constexpr char const* c_forkOrder[8] = {
    "regolith", "canyon", "ecotone", "fjord", "granite", "holocene", "isthmus", "jovian"};

int forkRank(std::string const& fork)
{
    for (int i = 0; i < 8; ++i)
    {
        if (fork == c_forkOrder[i])
        {
            return i;
        }
    }
    return -1;
}

// Boundary goldens carry the NEW fork's header although the case name keeps the old
// prefix (activation / synthesis blocks). Probed: these three and only these.
std::string forkOfGolden(std::string const& filename)
{
    static std::map<std::string, std::string> const c_overrides = {
        {"canyon_boundary_ecotone_synth.golden.json", "ecotone"},
        {"canyon_boundary_ecotone_activation.golden.json", "ecotone"},
        {"fjord_upgrade_isthmus_activation.golden.json", "isthmus"},
    };
    if (auto it = c_overrides.find(filename); it != c_overrides.end())
    {
        return it->second;
    }
    return filename.substr(0, filename.find('_'));
}

// chained/ names carry no fork prefix: the chain cases are isthmus-era, the jovian*
// ones jovian-era (both tiers share the same tail set, so this only affects reporting).
std::string chainedForkOf(std::string const& name)
{
    return name.rfind("jovian", 0) == 0 ? std::string{"jovian"} : std::string{"isthmus"};
}

// The six optional tail slots, in RLP order, rendered as a printable presence tuple.
// Read off the DECODED header, so presence is the decoder's positional model (a later
// slot present implies the earlier ones were present) — no hand-written RLP walk.
std::string tailPresence(bcos::protocol::EthBlockHeaderData const& d)
{
    std::string out;
    auto add = [&out](bool present, char const* name) {
        if (present)
        {
            if (!out.empty())
            {
                out += "+";
            }
            out += name;
        }
    };
    add(d.baseFee.has_value(), "baseFeePerGas");
    add(d.withdrawalsHash.has_value(), "withdrawalsRoot");
    add(d.blobGasUsed.has_value(), "blobGasUsed");
    add(d.excessBlobGas.has_value(), "excessBlobGas");
    add(d.parentBeaconRoot.has_value(), "parentBeaconBlockRoot");
    add(d.requestsHash.has_value(), "requestsHash");
    return out;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(OpHeaderFieldSetBaselineSuite)

// clang-format off
BOOST_AUTO_TEST_CASE(GoldenHeaderFieldSetAndReencodeMatchForkBaseline, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto const dir = fs::path(OP_T8N_GOLDEN_ENGINE_DIR);
    if (!fs::exists(dir))
    {
        // House pattern (see OpEnginePayloadShapeBaselineTest.cpp:44-52): absent locally
        // is a skip with a pointer to the ritual; absent in CI is a FAILURE, because
        // GITHUB_ACTIONS defines FISCO_REQUIRE_T8N_CORPUS and a silently empty sweep is
        // exactly the degradation this test exists to prevent.
#ifdef FISCO_REQUIRE_T8N_CORPUS
        BOOST_FAIL(
            "CI requires the golden corpus at " << dir.string() << " -- run the t8n regen ritual");
#else
        BOOST_TEST_MESSAGE("golden corpus absent; run the corpus regen ritual -- skipping");
        return;
#endif
    }

    std::map<std::string, std::string> presenceByFork;  // fork -> tail presence tuple
    std::map<std::string, std::uint32_t> filesByFork;   // fork -> golden count
    std::uint32_t filesChecked = 0;

    auto checkOne = [&](std::string const& name, std::string const& hexWithPrefix,
                        std::string const& fork) {
        BOOST_TEST_INFO_SCOPE(name);
        BOOST_REQUIRE_GT(hexWithPrefix.size(), 2U);
        auto const rank = forkRank(fork);
        BOOST_REQUIRE_MESSAGE(rank >= 0, name << ": unknown fork tier " << fork);

        auto bytes = bcos::fromHex(hexWithPrefix);  // tolerates the 0x prefix
        BOOST_REQUIRE(!bytes.empty());
        bcos::protocol::EthBlockHeader eth;
        auto err = eth.rlpDecode(bcos::ref(bytes));
        BOOST_REQUIRE_MESSAGE(err == nullptr,
            name << ": rlpDecode of encodedHeaderHex failed: " << (err ? err->errorMessage() : ""));
        bcos::bytes reencoded;
        eth.rlpEncode(reencoded);

        // (a) decisive: our encoder must reproduce op-geth's bytes exactly. On mismatch
        // localize with the same data the decoder already gave us (no RLP walk needed).
        if (reencoded != bytes)
        {
            std::size_t i = 0;
            while (i < std::min(reencoded.size(), bytes.size()) && reencoded[i] == bytes[i])
            {
                ++i;
            }
            BOOST_CHECK_MESSAGE(false,
                name << ": re-encode differs from the golden header -- first difference at byte "
                     << i << " (golden " << bytes.size() << "B, re-encoded " << reencoded.size()
                     << "B); decoded tail presence [" << tailPresence(eth.data())
                     << "]. A field-set/order change at this fork, or a divergence in the "
                        "optional-tail semantics, must be registered in DIVERGENCES.md.");
        }

        // (b) presence invariants: uniform within a tier.
        auto const tuple = tailPresence(eth.data());
        if (auto it = presenceByFork.find(fork); it == presenceByFork.end())
        {
            presenceByFork[fork] = tuple;
        }
        else
        {
            BOOST_CHECK_MESSAGE(it->second == tuple,
                name << ": fork " << fork << " tail presence [" << tuple
                     << "] differs from this tier's other goldens [" << it->second << "]");
        }
        ++filesByFork[fork];
        ++filesChecked;
    };

    for (auto const& entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json" ||
            entry.path().stem().extension() != ".golden")
        {
            continue;
        }
        std::ifstream in(entry.path());
        Json::Value doc;
        BOOST_REQUIRE_MESSAGE(Json::Reader{}.parse(in, doc, false),
            "malformed golden json: " << entry.path().string());
        checkOne(entry.path().filename().string(), doc["encodedHeaderHex"].asString(),
            forkOfGolden(entry.path().filename().string()));
    }
    auto const chained = dir / "chained";
    if (fs::exists(chained))
    {
        for (auto const& entry : fs::directory_iterator(chained))
        {
            auto name = entry.path().filename().string();
            if (!entry.is_regular_file() || entry.path().extension() != ".json" ||
                name.find(".golden.json") == std::string::npos)
            {
                continue;
            }
            std::ifstream in(entry.path());
            Json::Value doc;
            BOOST_REQUIRE_MESSAGE(Json::Reader{}.parse(in, doc, false),
                "malformed chained golden: " << entry.path().string());
            checkOne(name, doc["encodedHeaderHex"].asString(), chainedForkOf(name));
        }
    }

    // ---- boundary facts + monotonicity (the readable half of the baseline) ----
    for (auto const& [fork, tuple] : presenceByFork)
    {
        auto const rank = forkRank(fork);
        BOOST_CHECK_MESSAGE(tuple.find("baseFeePerGas") != std::string::npos,
            fork << ": baseFeePerGas must be present on every Eth header tier");
        if (rank >= 1)
            BOOST_CHECK_MESSAGE(tuple.find("withdrawalsRoot") != std::string::npos,
                fork << ": Canyon+ must carry withdrawalsRoot");
        else
            BOOST_CHECK_MESSAGE(tuple.find("withdrawalsRoot") == std::string::npos,
                fork << ": pre-Canyon must NOT carry withdrawalsRoot");
        if (rank >= 2)
        {
            BOOST_CHECK_MESSAGE(tuple.find("parentBeaconBlockRoot") != std::string::npos,
                fork << ": Ecotone+ must carry parentBeaconBlockRoot");
            BOOST_CHECK_MESSAGE(tuple.find("blobGasUsed") != std::string::npos,
                fork << ": Ecotone+ must carry the blob pair");
            BOOST_CHECK_MESSAGE((tuple.find("blobGasUsed") != std::string::npos) ==
                                    (tuple.find("excessBlobGas") != std::string::npos),
                fork << ": blobGasUsed/excessBlobGas must appear together");
        }
        else
            BOOST_CHECK_MESSAGE(tuple.find("parentBeaconBlockRoot") == std::string::npos,
                fork << ": pre-Ecotone must NOT carry parentBeaconBlockRoot");
        if (rank >= 6)
            BOOST_CHECK_MESSAGE(tuple.find("requestsHash") != std::string::npos,
                fork << ": Isthmus+ must carry requestsHash");
        else
            BOOST_CHECK_MESSAGE(tuple.find("requestsHash") == std::string::npos,
                fork << ": pre-Isthmus must NOT carry requestsHash");
    }
    for (int i = 1; i < 8; ++i)
    {
        auto const lo = presenceByFork.find(c_forkOrder[i - 1]);
        auto const hi = presenceByFork.find(c_forkOrder[i]);
        if (lo == presenceByFork.end() || hi == presenceByFork.end())
        {
            continue;
        }
        for (auto const* field : {"baseFeePerGas", "withdrawalsRoot", "blobGasUsed",
                 "excessBlobGas", "parentBeaconBlockRoot", "requestsHash"})
        {
            if (lo->second.find(field) != std::string::npos)
            {
                BOOST_CHECK_MESSAGE(hi->second.find(field) != std::string::npos,
                    c_forkOrder[i - 1] << " carries " << field << " but " << c_forkOrder[i]
                                       << " does not -- per-fork field sets must only grow");
            }
        }
    }

    // The sweep must actually compare the corpus, and at least eight distinct forks
    // (regolith..jovian) must be present — otherwise this degraded to an empty pass.
    BOOST_CHECK_GE(filesChecked, 100U);
    BOOST_CHECK_MESSAGE(presenceByFork.size() >= 8,
        "compared " << presenceByFork.size() << " fork tiers, need >= 8");
    for (auto const* fork : c_forkOrder)
    {
        if (auto it = presenceByFork.find(fork); it != presenceByFork.end())
        {
            BOOST_TEST_MESSAGE("  " << fork << ": " << filesByFork[fork]
                                    << " golden(s), tail presence [" << it->second << "]");
        }
    }
    BOOST_TEST_MESSAGE(
        "karst: NO golden in the corpus -- asserted by construction, not measured: "
        "same tail set as jovian (no new header field on the Osaka EL ruleset) and "
        "no slotNumber; the byte-equivalence half above is what would catch its "
        "appearance in a future corpus.");
}
BOOST_AUTO_TEST_SUITE_END()
