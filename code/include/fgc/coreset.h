#pragma once
#ifndef FGC_CORESETS_H__
#define FGC_CORESETS_H__
#include <vector>
#include <map>
//#define ALIAS_THREADSAFE 0
#include "alias_sampler/alias_sampler.h"
#include "shared.h"
#include <zlib.h>
#ifdef _OPENMP
#  include <omp.h>
#endif
#include "fgc/distance.h"
#include "blaze_adaptor.h"


namespace fgc {
namespace coresets {
#ifndef SMALLARRAY_DEFAULT_N
#define SMALLARRAY_DEFAULT_N 16
#endif

template<typename IT, typename Alloc=std::allocator<IT>, size_t N=SMALLARRAY_DEFAULT_N>
using IVec = blaze::SmallArray<IT, N, Alloc>;
template<typename FT=float, typename Alloc=std::allocator<FT>, size_t N=SMALLARRAY_DEFAULT_N>
using WVec = blaze::SmallArray<FT, N, Alloc>;

using namespace shared;

enum SensitivityMethod: int {
    BRAVERMAN_FELDMAN_LANG, // 2016, New Frameowkrs
    FELDMAN_LANGBERG,       // 2011, Unified Framework
    LUCIC_FAULKNER_KRAUSE_FELDMAN, // 2017, Training Gaussian Mixture Models at Scale
    // aliases
    BFL=BRAVERMAN_FELDMAN_LANG,
    FL=FELDMAN_LANGBERG,
    LFKF=LUCIC_FAULKNER_KRAUSE_FELDMAN
};

template<typename IT, typename FT>
struct IndexCoreset {
    static_assert(std::is_integral<IT>::value, "IT must be integral");
    static_assert(std::is_floating_point<FT>::value, "FT must be floating-point");
    /*
     * consists of only indices and weights
     */
    blaze::DynamicVector<IT> indices_;
    blaze::DynamicVector<FT> weights_;
    size_t size() const {return indices_.size();}
    IndexCoreset(IndexCoreset &&o) = default;
    IndexCoreset(const IndexCoreset &o) = default;
    void compact(bool shrink_to_fit=false) {
        // TODO: replace with hash map and compact
        std::map<std::pair<IT, FT>, uint32_t> m;
        for(IT i = 0; i < indices_.size(); ++i) {
            ++m[std::make_pair(indices_[i], weights_[i])];
            //++m[std::make_pair(p.first, p.second)];
        }
        if(m.size() == indices_.size()) return;
        auto it = &indices_[0];
        auto wit = &weights_[0];
        for(const auto &pair: m) {
            auto [idxw, count] = pair;
            auto [idx, weight] = idxw;
            *it++ = idx;
            *wit++ = count * weight; // Add the weights together
        }
        size_t newsz = m.size();
        assert(newsz < indices_.size());
        indices_.resize(newsz);
        weights_.resize(newsz);
        if(shrink_to_fit) indices_.shrinkToFit(), weights_.shrinkToFit();
#if 0
        std::fprintf(stderr, "compacted\n");
#endif
    }
    std::vector<std::pair<IT, FT>> to_pairs() const {
        std::vector<std::pair<IT, FT>> ret(size());
        OMP_PFOR
        for(IT i = 0; i < size(); ++i)
            ret[i].first = indices_[i], ret[i].second = weights_[i];
        return ret;
    }
    void show() {
        for(size_t i = 0, e  = indices_.size(); i < e; ++i) {
            std::fprintf(stderr, "%zu: [%u/%g]\n", i, indices_[i], weights_[i]);
        }
    }
    IndexCoreset(size_t n): indices_(n), weights_(n) {}
};

#if 0
#ifdef __AVX512F__
#define USE_VECTORS
#define VECTOR_WIDTH 64u
template<typename FT>
struct VT;
template<> VT<float>{using type = __m512;}
template<> VT<double>{using type = __m512d;}
#elif defined(__AVX2__)
template<typename FT>
struct VT;
template<> VT<float>{using type = __m512;}
template<> VT<double>{using type = __m512d;}
#define VECTOR_WIDTH 32u
#elif defined(__SSE2__)
#define VECTOR_WIDTH 16u
#endif
#endif


template<typename T>
struct dumbrange {
    T beg, e_;
    dumbrange(T beg, T end): beg(beg), e_(end) {}
    auto begin() const {return beg;}
    auto end()   const {return e_;}
};
template<typename T>
auto make_dumbrange(T beg, T end) {return dumbrange<T>(beg, end);}

template<typename FT=float, typename IT=std::uint32_t>
struct UniformSampler {
    const size_t np_;
    wy::WyRand<IT, 2> rng_;
    UniformSampler(size_t np, uint64_t seed=0): np_(np), rng_(seed) {
    }
    IndexCoreset<IT, FT> sample(const size_t n, uint64_t seed=0) {
        if(seed) rng_.seed(seed);
        IndexCoreset<IT, FT> ret(n);
        for(size_t i = 0; i < n; ++i) {
            ret.indices_[i] = rng_() % np_;
        }
        ret.weights_ = static_cast<FT>(np_) / n; // Ensure final weight = np_
        return ret;
    }
};

template<typename FT=float, typename IT=std::uint32_t>
struct CoresetSampler {
    using Sampler = alias::AliasSampler<FT, wy::WyRand<IT, 2>, IT>;
    std::unique_ptr<Sampler> sampler_;
    std::unique_ptr<FT []>     probs_;
    std::unique_ptr<FT []>   weights_;
    size_t                        np_;
    uint64_t                    seed_ = 1337;


    bool ready() const {return sampler_.get();}

    bool operator==(const CoresetSampler &o) const {
        //VERBOSE_ONLY(std::fprintf(stderr, "np: %zu/%zu\n", np_, o.np_);)
        return np_ == o.np_ &&
                       std::equal(probs_.get(), probs_.get() + np_, o.probs_.get()) &&
                      ((weights_.get() == nullptr && o.weights_.get() == nullptr) || // Both are nullptr or
                        std::equal(weights_.get(), weights_.get() + np_, o.weights_.get())); // They're both the same
    }

    CoresetSampler(CoresetSampler &&o)      = default;
    CoresetSampler(const CoresetSampler &o) = delete;
    CoresetSampler(std::string s) {
        gzFile ifp = gzopen(s.data(), "rb");
        this->read(ifp);
        gzclose(ifp);
    }
    CoresetSampler() {}

    void write(const std::string &s) const {
        gzFile fp = gzopen(s.data(), "wb");
        if(!fp) throw std::runtime_error("Failed to open file");
        this->write(fp);
        gzclose(fp);
    }
    void read(const std::string &s) const {
        gzFile fp = gzopen(s.data(), "rb");
        if(!fp) throw std::runtime_error("Failed to open file");
        this->read(fp);
        gzclose(fp);
    }
    void write(gzFile fp) const {
        uint64_t n = np_;
        gzwrite(fp, &n, sizeof(n));
#if VERBOSE_AF
        std::fprintf(stderr, "Writing %zu\n", size_t(n));
#endif
        gzwrite(fp, &seed_, sizeof(seed_));
        gzwrite(fp, &probs_[0], sizeof(probs_[0]) * np_);
        uint32_t weights_present = weights_ ? 1337: 0;
        gzwrite(fp, &weights_present, sizeof(weights_present));
        if(weights_)
            gzwrite(fp, &weights_[0], sizeof(weights_[0]) * np_);
    }
    void write(std::FILE *fp) const {
        auto fd = ::fileno(fp);
        uint64_t n = np_;
        checked_posix_write(fd, &n, sizeof(n));
        checked_posix_write(fd, &seed_, sizeof(seed_));
        checked_posix_write(fd, &probs_[0], sizeof(probs_[0]) * np_);
        uint32_t weights_present = weights_ ? 1337: 0;
        checked_posix_write(fd, &weights_present, sizeof(weights_present));
        if(weights_)
            checked_posix_write(fd, &weights_[0], sizeof(weights_[0]) * np_);
    }
    void read(gzFile fp) {
        uint64_t n;
        gzread(fp, &n, sizeof(n));
#if VERBOSE_AF
        std::fprintf(stderr, "Reading %zu\n", size_t(n));
#endif
        np_ = n;
        gzread(fp, &seed_, sizeof(seed_));
        probs_.reset(new FT[n]);
        gzread(fp, &probs_[0], sizeof(FT) * n);
        uint32_t weights_present;
        gzread(fp, &weights_present, sizeof(weights_present));
        if(weights_present) {
            assert(weights_present == 1337);
            weights_.reset(new FT[n]);
            gzread(fp, &weights_[0], sizeof(FT) * n);
        }
        sampler_.reset(new Sampler(probs_.get(), probs_.get() + n, seed_));
    }
    void read(std::FILE *fp) {
        uint64_t n;
        auto fd = ::fileno(fp);
        ::read(fd, &n, sizeof(n));
        ::read(fd, &seed_, sizeof(seed_));
        probs_.reset(new FT[n]);
        ::read(fd, &probs_[0], sizeof(FT) * n);
        uint32_t weights_present;
        ::read(fd, &weights_present, sizeof(weights_present));
        if(weights_present) {
            assert(weights_present == 1337);
            weights_.reset(new FT[n]);
            ::read(fd, &weights_[0], sizeof(FT) * n);
        }
        sampler_.reset(new Sampler(probs_.get(), probs_.get() + n, seed_));
    }

    void make_gmm_sampler(size_t np, size_t ncenters,
                      const FT *costs, const IT *assignments,
                      const FT *weights=nullptr,
                      uint64_t seed=137,
                      double alpha_est=0.)
    {
        // Note: this takes actual distances and then squares them.
        // ensure that the costs provided are L2Norm, not sqrL2Norm.
        // From Training Gaussian Mixture Models at Scale via Coresets
        // http://www.jmlr.org/papers/volume18/15-506/15-506.pdf
        // Note: this can be expanded to general probability measures.
        // I should to generalize this, such as for negative binomial and/or fancier functions
        np_ = np;
        if(weights) {
            weights_.reset(new FT[np]);
            std::memcpy(&weights_[0], weights, sizeof(FT) * np);
        } else weights_.release();
        std::vector<FT> weight_sums(ncenters), weighted_cost_sums(ncenters);
        std::vector<FT> sqcosts(ncenters);
        std::vector<IT> center_counts(ncenters);
        double total_cost = 0.;

        // TODO: vectorize
        // Sketch: check for SSE2/AVX2/AVX512
        //         if weights is null,
        //         set a vector via Space::set1()
        //
        OMP_PRAGMA("omp parallel for reduction(+:total_cost)")
        for(size_t i = 0; i < np; ++i) {
            // TODO: vectorize?
            // weight sums per assignment couldn't be vectorized,
            // total costs could be
            // Probably a 4-16x speedup on 1/3 of the cost
            // So maybe like a ~30% speedup?
            auto asn = assignments[i];
            assert(asn < ncenters);
            const auto w = getweight(i);
            auto cost = costs[i] * costs[i]; // d^2(x, A)
            auto wcost = w * cost;
            OMP_ATOMIC
            weighted_cost_sums[asn] += wcost;
            OMP_ATOMIC
            weight_sums[asn] += w; // If unweighted, weights are 1.
            OMP_ATOMIC
            ++center_counts[asn];
            total_cost += wcost;
            sqcosts[i] = cost;
        }
        OMP_PFOR
        for(size_t i = 0; i < np; ++i) {
            this->probs_[i] = alpha_est * getweight(i) * (sqcosts[i] + weighted_cost_sums[assignments[i]] / weight_sums[assignments[i]])
                        + 2. * total_cost / weight_sums[assignments[i]];
        }
        auto si = 1. / std::accumulate(&this->probs_[0], this->probs_.get() + np, 0.);
        OMP_PFOR
        for(size_t i = 0; i < np; ++i)
            this->probs_[i] *= si;
        sampler_.reset(new Sampler(probs_.get(), probs_.get() + np, seed));
    }
    void make_sampler(size_t np, size_t ncenters,
                      const FT *costs, const IT *assignments,
                      const FT *weights=nullptr,
                      uint64_t seed=137,
                      SensitivityMethod sens=BRAVERMAN_FELDMAN_LANG,
                      double alpha_est=0.)
    {
        seed_ = seed;
        if(sens == LUCIC_FAULKNER_KRAUSE_FELDMAN) {
            make_gmm_sampler(np, ncenters, costs, assignments, weights, seed, alpha_est);
            return;
        }
        np_ = np;
        if(weights) {
            weights_.reset(new FT[np]);
            std::memcpy(&weights_[0], weights, sizeof(FT) * np);
        } else weights_.release();
        std::vector<FT> weight_sums(ncenters);
        std::vector<IT> center_counts(ncenters);
        double total_cost = 0.;

        OMP_PRAGMA("omp parallel for reduction(+:total_cost)")
        for(size_t i = 0; i < np; ++i) {
            // TODO: vectorize?
            // weight sums per assignment couldn't be vectorized,
            // total costs could be
            // Probably a 4-16x speedup on 1/3 of the cost
            // So maybe like a ~30% speedup?
            auto asn = assignments[i];
            assert(asn < ncenters);
            const auto w = getweight(i);

            OMP_ATOMIC
            weight_sums[asn] += w; // If unweighted, weights are 1.
            OMP_ATOMIC
            ++center_counts[asn];
            total_cost += w * costs[i];
        }
        assert(std::accumulate(center_counts.begin(), center_counts.end(), IT(0)) == np);
        for(const auto c: center_counts) std::fprintf(stderr, "center count: %u\n", c);
        for(const auto c: center_counts) assert(c);
        const bool is_feldman = (sens == FELDMAN_LANGBERG);
        probs_.reset(new FT[np]);
        std::fprintf(stderr, "Sum: %g. accumulate sum: %g\n", total_cost, std::accumulate(costs, costs + np, 0.));
        if(is_feldman) {
            // Ignores number of items assigned to each cluster
            sampler_.reset(new Sampler(probs_.get(), probs_.get() + np, seed));
            double total_cost_inv = 1. / (total_cost);
            double total_probs = 0.;
            OMP_PRAGMA("omp parallel for reduction(+:total_probs)")
            for(size_t i = 0; i < np; ++i) {
                probs_[i] = getweight(i) * (costs[i]) * total_cost_inv;
                total_probs += probs_[i];
            }
            blaze::CustomVector<FT, blaze::unaligned, blaze::unpadded>(probs_.get(), np) /= total_probs;
        } else {
            //for(auto i = 0u; i < ncenters; ++i)
            //    weight_sums[i] = 1./(2. * center_counts[i] * weight_sums[i]);
            //double total_cost_2inv = 1. / (2. * total_cost);
            double total_probs = 0.;
            OMP_PRAGMA("omp parallel for reduction(+:total_probs)")
            for(size_t i = 0; i < np; ++i) {
                const auto w = getweight(i);
                double fraccost = w * costs[i] / total_cost;
                const auto asn = assignments[i];
                double fracw = w / (weight_sums[asn] * center_counts[asn]);
                //std::fprintf(stderr, "w: %f. fraction of total cost: %g. fraction of weight for section: %g\n", getweight(i), fraccost, fracw);
                probs_[i] = .5 * (fraccost + fracw);
                total_probs += probs_[i];
            }
#ifndef NDEBUG
            std::fprintf(stderr, "total probs: %g\n", total_probs);
#endif
#if 0
            for(const auto p: make_dumbrange(probs_.get(), probs_.get() + np))
                std::fprintf(stderr, "p: %g\n", p);
#endif
            blaze::CustomVector<FT, blaze::unaligned, blaze::unpadded>(probs_.get(), np) /= total_probs;
        }
#ifndef NDEBUG
        double psum = 0.;
        for(size_t i = 0; i < np_; ++i) {
            psum += probs_[i];
        }
        std::fprintf(stderr, "psum: %f\n", psum);
#endif
        sampler_.reset(new Sampler(probs_.get(), probs_.get() + np, seed));
    }
    auto getweight(size_t ind) const {
        return weights_ ? weights_[ind]: static_cast<FT>(1.);
    }
    IndexCoreset<IT, FT> sample(const size_t n, uint64_t seed=0) {
        if(!sampler_.get()) throw 1;
        if(seed) sampler_->seed(seed);
        IndexCoreset<IT, FT> ret(n);
        const double nsamplinv = 1. / n;
        for(size_t i = 0; i < n; ++i) {
            auto ind = sampler_->sample();
            ret.indices_[i] = ind;
            ret.weights_[i] = getweight(ind) * nsamplinv / probs_[ind];
        }
#ifndef NDEBUG
        for(size_t i = 0; i < n; ++i)
            assert(ret.indices_[i] < np_);
#endif
        return ret;
    }
    size_t size() const {return np_;}
};

template<typename FT, bool SO=blaze::rowMajor>
struct MetricCoreset {
    blaze::DynamicMatrix<FT, SO> distances_;
};


}//coresets

}// namespace fgc

namespace cs = fgc::coresets;

#endif /* FGC_CORESETS_H__ */
