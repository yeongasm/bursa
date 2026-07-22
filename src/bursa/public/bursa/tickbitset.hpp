#pragma once
#ifndef BURSA_TICK_BITSET_HPP
#define BURSA_TICK_BITSET_HPP

namespace bursa
{
    // Two-level occupancy bitmap over a dense range of tick indices [0, tickCount).
    //
    //   leaf    : 1 bit per tick.        leaf[w] bit b  == "tick (w*64 + b) has >= 1 order"
    //   summary : 1 bit per leaf word.   summary bit k  == "leaf[k] is non-zero"
    //
    // Invariant: summary bit k is set  <=>  leaf[k] != 0.
    // We therefore only touch the summary when a leaf word transitions
    // empty <-> non-empty. set()/clear() are O(1); highest()/lowest() are two
    // bit-scans (one over the summary, one over a single leaf word).
//     class TickBitset
//     {
//     public:
//         static constexpr size_t bits_per_word = 64;
//         static constexpr size_t npos          = ~size_t{ 0 };

//         auto resize(size_t tickCount) -> void
//         {
//             size_t const leafWords    = word_count(tickCount);
//             size_t const summaryWords = word_count(leafWords);

//             m_leaf.assign(leafWords, 0);
//             m_summary.assign(summaryWords, 0);
//         }

//         // Mark tick `idx` as occupied.
//         auto set(size_t idx) -> void
//         {
//             auto const [w, b] = split(idx);

//             // Leaf word is about to become non-empty: reflect that in the summary.
//             if (m_leaf[w] == 0)
//             {
//                 auto const [sw, sb] = split(w);
//                 m_summary[sw] |= bit(sb);
//             }

//             m_leaf[w] |= bit(b);
//         }

//         // Mark tick `idx` as empty.
//         auto clear(size_t idx) -> void
//         {
//             auto const [w, b] = split(idx);

//             m_leaf[w] &= ~bit(b);

//             // Leaf word just became empty: clear its summary bit.
//             if (m_leaf[w] == 0)
//             {
//                 auto const [sw, sb] = split(w);
//                 m_summary[sw] &= ~bit(sb);
//             }
//         }

//         // Highest occupied tick (best bid = highest price), or npos if none.
//         auto highest() const -> size_t
//         {
//             // Walk summary words from the top; the first non-empty one holds the
//             // highest occupied leaf word.
//             for (size_t s = m_summary.size(); s-- > 0;)
//             {
//                 if (m_summary[s] != 0)
//                 {
//                     size_t const w = s * bits_per_word + highest_bit(m_summary[s]);
//                     return w * bits_per_word + highest_bit(m_leaf[w]);
//                 }
//             }
//             return npos;
//         }

//         // Lowest occupied tick (best ask = lowest price), or npos if none.
//         auto lowest() const -> size_t
//         {
//             for (size_t s = 0; s < m_summary.size(); ++s)
//             {
//                 if (m_summary[s] != 0)
//                 {
//                     size_t const w = s * bits_per_word + lowest_bit(m_summary[s]);
//                     return w * bits_per_word + lowest_bit(m_leaf[w]);
//                 }
//             }
//             return npos;
//         }

//     private:
//         static auto bit(size_t b) -> u64
//         {
//             return u64{ 1 } << b;
//         }

//         // Split a linear index into (word, bit-within-word).
//         static auto split(size_t idx) -> std::pair<size_t, size_t>
//         {
//             return { idx / bits_per_word, idx % bits_per_word };
//         }

//         static auto word_count(size_t bits) -> size_t
//         {
//             return (bits + bits_per_word - 1) / bits_per_word;
//         }

//         // Index of the most-significant set bit. Precondition: word != 0.
//         static auto highest_bit(u64 word) -> size_t
//         {
//             return (bits_per_word - 1) - static_cast<size_t>(std::countl_zero(word));
//         }

//         // Index of the least-significant set bit. Precondition: word != 0.
//         static auto lowest_bit(u64 word) -> size_t
//         {
//             return static_cast<size_t>(std::countr_zero(word));
//         }

//         std::vector<u64> m_leaf;
//         std::vector<u64> m_summary;
//     };
// }

#endif // !BURSA_TICK_BITSET_HPP