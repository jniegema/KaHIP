/******************************************************************************
 * flow_graph.h
 * *
 * Source of KaHIP -- Karlsruhe High Quality Partitioning.
 * Christian Schulz <christian.schulz.phone@gmail.com>
 *****************************************************************************/

#ifndef FLOW_GRAPH_636S5L2S
#define FLOW_GRAPH_636S5L2S

#include <vector>

#include "definitions.h"

struct rEdge {
    NodeID     source;
    NodeID     target;
    FlowType   capacity;
    FlowType   flow;
    EdgeID     reverse_edge_index;

    rEdge() = default;
    rEdge( NodeID source, NodeID target, FlowType capacity, FlowType flow, EdgeID reverse_edge_index) {
        this->source             = source;
        this->target             = target;
        this->capacity           = capacity;
        this->flow               = flow;
        this->reverse_edge_index = reverse_edge_index;
    }

};

// this is a adjacency list implementation of the residual graph
// for each edge we create, we create a rev edge with cap 0
// zero capacity edges are residual edges
//
// The edges of all nodes sit in one array, each node's in the order new_edge
// added them, so every edge has the index (and its reverse the reverse
// index) it had in the per-node vectors this replaces: push-relabel visits
// them in the same order and finds the same cut. The per-node vectors cost
// one allocation per node and per growth; building the flow problems was 12%
// of a node ordering's CPU time on an FDFD graph, most of it in the tries.
// finish_construction() lays the edges out; push_relabel calls it, so
// builders that never did keep working.
class flow_graph {
public:
        flow_graph() {
                m_num_edges = 0;
                m_num_nodes = 0;
        };

        virtual ~flow_graph() {};

        void start_construction(NodeID nodes, EdgeID edges = 0) {
                m_num_nodes = nodes;
                m_num_edges = edges;
                m_pending.clear();
                m_first.clear();
                m_edges.clear();
                m_finished = false;
        }

        void finish_construction() {
                if (m_finished) return;
                m_first.assign(m_num_nodes + 1, 0);
                for (const pending_edge & p : m_pending) {
                        m_first[p.source + 1]++;
                        m_first[p.target + 1]++;
                }
                for (NodeID v = 0; v < m_num_nodes; v++) m_first[v + 1] += m_first[v];
                std::vector<EdgeID> next(m_first.begin(), m_first.end() - 1);
                m_edges.resize(m_first[m_num_nodes]);
                for (const pending_edge & p : m_pending) {
                        const EdgeID forward  = next[p.source]++;
                        const EdgeID backward = next[p.target]++;
                        m_edges[forward]  = rEdge(p.source, p.target, p.capacity, 0, backward - m_first[p.target]);
                        m_edges[backward] = rEdge(p.target, p.source, 0, 0, forward - m_first[p.source]);
                }
                std::vector<pending_edge>().swap(m_pending);
                m_finished = true;
        };

        NodeID number_of_nodes() {return m_num_nodes;};
        EdgeID number_of_edges() {return m_num_edges;};

        NodeID getEdgeTarget(NodeID source, EdgeID e);
        NodeID getEdgeCapacity(NodeID source, EdgeID e);

        FlowType getEdgeFlow(NodeID source, EdgeID e);
        void setEdgeFlow(NodeID source, EdgeID e, FlowType flow);

        EdgeID getReverseEdge(NodeID source, EdgeID e);

        void new_edge(NodeID source, NodeID target, FlowType capacity) {
               m_pending.push_back(pending_edge{source, target, capacity});
               m_num_edges += 2;
        };

        EdgeID get_first_edge(NodeID node) {return 0;};
        EdgeID get_first_invalid_edge(NodeID node) {return m_first[node + 1] - m_first[node];};


private:
        struct pending_edge {
                NodeID   source;
                NodeID   target;
                FlowType capacity;
        };
        std::vector<pending_edge> m_pending;
        std::vector<EdgeID> m_first;
        std::vector<rEdge> m_edges;
        bool m_finished = false;
        NodeID m_num_nodes;
        NodeID m_num_edges;
};

inline
NodeID flow_graph::getEdgeCapacity(NodeID source, EdgeID e) {
#ifdef NDEBUG
        return m_edges[m_first[source] + e].capacity;
#else
        return m_edges.at(m_first.at(source) + e).capacity;
#endif
};

inline
void flow_graph::setEdgeFlow(NodeID source, EdgeID e, FlowType flow) {
#ifdef NDEBUG
        m_edges[m_first[source] + e].flow = flow;
#else
        m_edges.at(m_first.at(source) + e).flow = flow;
#endif
};

inline
FlowType flow_graph::getEdgeFlow(NodeID source, EdgeID e) {
#ifdef NDEBUG
        return m_edges[m_first[source] + e].flow;
#else
        return m_edges.at(m_first.at(source) + e).flow;
#endif
};

inline
NodeID flow_graph::getEdgeTarget(NodeID source, EdgeID e) {
#ifdef NDEBUG
        return m_edges[m_first[source] + e].target;
#else
        return m_edges.at(m_first.at(source) + e).target;
#endif
};

inline
EdgeID flow_graph::getReverseEdge(NodeID source, EdgeID e) {
#ifdef NDEBUG
        return m_edges[m_first[source] + e].reverse_edge_index;
#else
        return m_edges.at(m_first.at(source) + e).reverse_edge_index;
#endif

}

#endif /* end of include guard: FLOW_GRAPH_636S5L2S */
