#pragma once
#include "boost/graph/adjacency_list.hpp"
#include "boost/graph/topological_sort.hpp"
#include "robin_hood.h"
#include "flat_hash_map/flat_hash_map.hpp"

namespace graph {

template<typename T, typename H = std::hash<T>, typename E = std::equal_to<T>, typename A = std::allocator<T> >
using flat_hash_set = ska::flat_hash_set<T, H, E, A>;

template <typename Key, typename T, typename Hash = robin_hood::hash<Key>,
          typename KeyEqual = std::equal_to<Key>, size_t MaxLoadFactor100 = 80>
using flat_hash_map = robin_hood::unordered_flat_map<Key, T, Hash, KeyEqual, MaxLoadFactor100>;

// May replace with robin_hood if they implement this.

using boost::vecS;
using boost::undirectedS;
using boost::directedS;
using boost::bidirectionalS;
using boost::vertex_index;
using boost::vertex_index_t;
using boost::graph_traits;

template<typename DirectedS=undirectedS, typename EdgeProps=float, typename VtxProps=boost::no_property,
         typename GraphProps=boost::no_property>
struct Graph: boost::adjacency_list<vecS, vecS, DirectedS, VtxProps, EdgeProps, GraphProps> {
    using super = boost::adjacency_list<vecS, vecS, DirectedS, VtxProps, EdgeProps, GraphProps>;
    using this_type = Graph<DirectedS, EdgeProps, VtxProps, GraphProps>;

    template<typename...Args>
    Graph(Args &&... args): super(std::forward<Args>(args)...) {
    }
    size_t num_edges() const {return boost::num_edges(*this);}
    size_t num_vertices() const {return boost::num_vertices(*this);}

    using edge_iterator         = decltype(boost::edges(std::declval<this_type>()).first);
    using edge_const_iterator   = decltype(boost::edges(std::declval<std::add_const_t<this_type>>()).first);
    using vertex_iterator       = decltype(boost::vertices(std::declval<this_type>()).first);
    using vertex_const_iterator = decltype(boost::vertices(std::declval<std::add_const_t<this_type>>()).first);
    using adjacency_iterator    = typename graph_traits<Graph>::adjacency_iterator;
    using Vertex                = typename graph_traits<Graph>::vertex_descriptor;
    static_assert(std::is_same_v<edge_iterator, edge_const_iterator>, "are they tho?");
    static_assert(std::is_same_v<vertex_iterator, vertex_const_iterator>, "are they tho?");

    struct Vertices {
        vertex_iterator f_;
        vertex_iterator e_;
        Vertices(Graph &ref) {
            std::tie(f_, e_) = boost::vertices(ref);
        }
        auto begin() const {
            return f_;
        }
        auto end() const {
            return e_;
        }
    };
    struct ConstVertices {
        vertex_const_iterator f_;
        vertex_const_iterator e_;
        ConstVertices(Graph &ref) {
            std::tie(f_, e_) = boost::vertices(ref);
        }
        auto begin() const {
            return f_;
        }
        auto end() const {
            return e_;
        }
    };
    struct Edges {
        edge_iterator f_;
        edge_iterator e_;
        Edges(Graph &ref) {
            std::tie(f_, e_) = boost::edges(ref);
        }
        auto begin() const {
            return f_;
        }
        auto end() const {
            return e_;
        }
    };
    struct ConstEdges {
        edge_const_iterator f_;
        edge_const_iterator e_;
        ConstEdges(const Graph &ref) {
            std::tie(f_, e_) = boost::edges(ref);
        }
        auto begin() const {
            return f_;
        }
        auto end() const {
            return e_;
        }
    };
    struct Adjacencies {
        adjacency_iterator f_, e_;
        Adjacencies(Vertex vd, const Graph &ref) {
            std::tie(f_, e_) = boost::adjacent_vertices(vd, ref);
        }
        auto begin() const {return f_;}
        auto end()   const {return e_;}
    };
    auto edges() {
        return Edges(*this);
    }
    auto cedges() const {
        return Edges(*this);
    }
    auto edges() const {return cedges();}
    auto vertices() {
        return Vertices(*this);
    }
    auto vertices() const {
        return cvertices();
    }
    auto cvertices() const {
        return Vertices(*this);
    }
    template<typename F>
    void for_each_edge(const F &f) {
        auto edges = edges();
        std::for_each(edges.begin(), edges.end(), f);
    }
    template<typename F>
    void for_each_edge(const F &f) const {
        auto edges = edges();
        std::for_each(edges.begin(), edges.end(), f);
    }
    template<typename F>
    void for_each_vertex(const F &f) {
        auto vertices = vertices();
        std::for_each(vertices.begin(), vertices.end(), f);
    }
    template<typename F>
    void for_each_vertex(const F &f) const {
        auto vertices = vertices();
        std::for_each(vertices.begin(), vertices.end(), f);
    }
    struct Deleter {
        template<typename T>
        void operator()(const T *x) const {
            std::free(const_cast<void *>(static_cast<const void *>(x)));
        }
    };
    template<typename Allocator=std::allocator<Vertex>>
    auto toposort() const {
        // TODO: consider doing it as
#if 0
        std::vector<Vertex, Allocator> ret(num_vertices());
        auto rit = ret.rbegin();
        // Or
        // std::pair<std::unique_ptr<Vertex[], Deleter>, size_t> ret{static_cast<Vertex *>(std::malloc(num_vertices() * sizeof(Vertex))), num_vertices()};
        // boost::topological_sort(*this, std::reverse_iterator<Vertex *>(ret.first.get() + ret.second));
        boost::topological_sort(*this, rit);
#else
        std::vector<Vertex, Allocator> ret;
        ret.reserve(num_vertices());
        boost::topological_sort(*this, std::back_inserter(ret));
        std::reverse(ret.begin(), ret.end());
#endif
        return ret;
    }
};
template<typename EdgeProps=float, typename VtxProps=boost::no_property,
         typename GraphProps=boost::no_property>
struct DirGraph: public Graph<directedS, EdgeProps, VtxProps, GraphProps> {
    using super = Graph<directedS, EdgeProps, VtxProps, GraphProps>;
    template<typename...Args>
    DirGraph(Args &&... args): super(std::forward<Args>(args)...) {
    }
};
template<typename EdgeProps=float, typename VtxProps=boost::no_property,
         typename GraphProps=boost::no_property>
struct UndirGraph: public Graph<undirectedS, EdgeProps, VtxProps, GraphProps> {
    using super = Graph<undirectedS, EdgeProps, VtxProps, GraphProps>;
    template<typename...Args>
    UndirGraph(Args &&... args): super(std::forward<Args>(args)...) {
    }
};
template<typename EdgeProps=float, typename VtxProps=boost::no_property,
         typename GraphProps=boost::no_property>
struct BidirGraph: public Graph<bidirectionalS, EdgeProps, VtxProps, GraphProps> {
    using super = Graph<bidirectionalS, EdgeProps, VtxProps, GraphProps>;
    template<typename...Args>
    BidirGraph(Args &&... args): super(std::forward<Args>(args)...) {
    }
};

} // graph
