/******************************************************************************
 * maxNodeHeap.h
 * *
 * Source of KaHIP -- Karlsruhe High Quality Partitioning.
 * Christian Schulz <christian.schulz.phone@gmail.com>
 *****************************************************************************/

#ifndef MAX_NODE_HEAP_39CK1B8I
#define MAX_NODE_HEAP_39CK1B8I

#include <limits>
#include <vector>
#include <unordered_map>
#ifndef _WIN32
#include <execinfo.h>
#endif

#include "data_structure/priority_queues/priority_queue_interface.h"

typedef int Key;

template < typename Data >
class QElement {
        public:
                QElement( Data data, Key key, int index ) : m_data(data), m_key (key), m_index(index) {};
                virtual ~QElement() {};

                Data & get_data() {
                        return m_data;
                }

                void set_data(Data & data) {
                        m_data = data;
                }

                Key get_key() {
                        return m_key;
                }

                void set_key(Key key) {
                        m_key = key;
                }

                int get_index() {
                        return m_index;
                }

                void set_index(int index) {
                        m_index = index;
                }

        private:
                Data m_data;
                Key  m_key;
                int  m_index; // the index of the element in the heap

};

class maxNodeHeap : public priority_queue_interface {
        public:

                struct Data {
                        NodeID node;
                        Data( NodeID node ) : node(node) {};
                };

                typedef QElement<Data> PQElement;

                maxNodeHeap() {};
                ~maxNodeHeap() override = default;

                NodeID size() override;
                bool empty() override;

                bool contains(NodeID node) override;

                // Whether node is in the heap under its own element, and that
                // element is not element 0. changeKey on an absent node acts on
                // element 0 (index_of), so only such a node's key is certain to
                // be changed by nothing but its own changeKey calls.
                bool owns_key(NodeID node) {
                        if( !has(node) ) return false;
                        const int i = m_element_index[node];
                        return i != 0 && m_elements[i].get_data().node == node;
                }
                void insert(NodeID id, Gain gain) override;

                NodeID deleteMax() override;
                void deleteNode(NodeID node) override;
                NodeID maxElement() override;
                Gain maxValue() override;

                void decreaseKey(NodeID node, Gain gain) override;
                void increaseKey(NodeID node, Gain gain) override;
                void changeKey(NodeID node, Gain gain) override;
                Gain getKey(NodeID node) override;

        private:
                std::vector< PQElement >               m_elements;      // elements that contain the data
                // Index of each node in m_elements, -1 when absent. A dense array instead of
                // std::unordered_map; index_of() reproduces the map's operator[], which inserts
                // an absent node with index 0, so the heap behaves exactly as before.
                std::vector<int>                   m_element_index;
                int& slot(NodeID node) {
                        if( node >= m_element_index.size() ) {
                                m_element_index.resize(std::max<std::size_t>(node + 1, 2 * m_element_index.size()), -1);
                        }
                        return m_element_index[node];
                }
                int& index_of(NodeID node) { int& s = slot(node); if( s == -1 ) s = 0; return s; }
                bool has(NodeID node) const { return node < m_element_index.size() && m_element_index[node] != -1; }
                void forget(NodeID node) { if( node < m_element_index.size() ) m_element_index[node] = -1; }
                std::vector< std::pair<Key, int> >     m_heap;          // key and index in elements (pointer)

                void siftUp( int pos );
                void siftDown( int pos );

};



inline Gain maxNodeHeap::maxValue() {
        return m_heap[0].first;
};

inline NodeID maxNodeHeap::maxElement() {
        return m_elements[m_heap[0].second].get_data().node;
};

inline void maxNodeHeap::siftDown( int pos ) {

        int curKey   = m_heap[pos].first;
        int lhsChild = 2*pos+1;
        int rhsChild = 2*pos+2;
        if( rhsChild < (int) m_heap.size() ) {

                int lhsKey = m_heap[lhsChild].first;
                int rhsKey = m_heap[rhsChild].first;

                if( lhsKey < curKey && rhsKey < curKey) {
                        return; // we are done
                } else {
                        //exchange with the larger one (maxHeap)
                        int swap_pos = lhsKey > rhsKey ? lhsChild : rhsChild;
                        std::swap( m_heap[pos], m_heap[swap_pos]);

                        int element_pos = m_heap[pos].second;
                        m_elements[element_pos].set_index(pos);

                        element_pos = m_heap[swap_pos].second;
                        m_elements[element_pos].set_index(swap_pos);

                        siftDown(swap_pos);
                        return;
                }

        } else if ( lhsChild < (int)m_heap.size()) {
                if( m_heap[pos].first < m_heap[lhsChild].first) {
                        std::swap( m_heap[pos], m_heap[lhsChild]);

                        int element_pos = m_heap[pos].second;
                        m_elements[element_pos].set_index(pos);

                        element_pos = m_heap[lhsChild].second;
                        m_elements[element_pos].set_index(lhsChild);

                        siftDown(lhsChild);
                        return;
                } else {
                        return; // we are done
                }
        }
}

inline void maxNodeHeap::siftUp( int pos ) {
            if( pos > 0 ) {
                    int parentPos = (int)(pos-1)/2;
                    if(  m_heap[parentPos].first < m_heap[pos].first) {
                            //heap condition not fulfulled
                            std::swap(m_heap[parentPos], m_heap[pos]);

                            int element_pos = m_heap[pos].second;
                            m_elements[element_pos].set_index(pos);

                            // update the heap index in the element
                            element_pos = m_heap[parentPos].second;
                            m_elements[element_pos].set_index(parentPos);

                            siftUp( parentPos );
                    }

            }
}

inline NodeID maxNodeHeap::size() {
        return m_heap.size();
}

inline bool maxNodeHeap::empty( ) {
        return m_heap.empty();
}

inline void maxNodeHeap::insert(NodeID node, Gain gain) {
        if( !has(node) ) {
                int element_index =  m_elements.size();
                int heap_size     =  m_heap.size();

                m_elements.push_back( PQElement( Data(node), gain, heap_size) );
                m_heap.push_back( std::pair< Key, int>(gain, element_index) );
                slot(node) = element_index;
                siftUp( heap_size );
        }
}

inline void maxNodeHeap::deleteNode(NodeID node) {
        int element_index = index_of(node);
        int heap_index    = m_elements[element_index].get_index();

        forget(node);

        std::swap( m_heap[heap_index], m_heap[m_heap.size() - 1]);
        //update the position of its element in the element array
        m_elements[m_heap[heap_index].second].set_index(heap_index);

        // we dont want holes in the elements array -- delete the deleted element from the array
        if(element_index != (int)(m_elements.size() - 1)) {
                std::swap( m_elements[element_index], m_elements[m_elements.size() - 1]);
                m_heap[ m_elements[element_index].get_index() ].second = element_index;
                int cnode              = m_elements[element_index].get_data().node;
                slot(cnode) = element_index;
        }

        m_elements.pop_back();
        m_heap.pop_back();

        if( m_heap.size() > 1 && heap_index < (int)m_heap.size() ) {
                //fix the max heap property
                siftDown(heap_index);
                siftUp(heap_index);
        }
};

inline NodeID maxNodeHeap::deleteMax() {
        if( m_heap.size() > 0) {
                int element_index = m_heap[0].second;
                int node = m_elements[element_index].get_data().node;
                forget(node);

                m_heap[0] = m_heap[m_heap.size() - 1];
                //update the position of its element in the element array
                m_elements[m_heap[0].second].set_index(0);

                // we dont want holes in the elements array -- delete the deleted element from the array
                if(element_index != (int)(m_elements.size() - 1)) {
                        m_elements[element_index] = m_elements[m_elements.size() - 1];
                        m_heap[ m_elements[element_index].get_index() ].second = element_index;
                        int cnode              = m_elements[element_index].get_data().node;
                        slot(cnode) = element_index;
                }

                m_elements.pop_back();
                m_heap.pop_back();

                if( m_heap.size() > 1) {
                        //fix the heap property
                        siftDown(0);
                }

                return node;
        }

        return -1;
}

inline void maxNodeHeap::changeKey(NodeID node, Gain gain) {
        Gain old_gain = m_heap[m_elements[index_of(node)].get_index()].first;
        if( old_gain > gain ) {
                decreaseKey(node, gain);
        } else if ( old_gain < gain ) {
                increaseKey(node, gain);
        }
};

inline void maxNodeHeap::decreaseKey(NodeID node, Gain gain) {
        ASSERT_TRUE(has(node));
        int queue_idx = index_of(node);
        int heap_idx  = m_elements[queue_idx].get_index();
        m_elements[queue_idx].set_key(gain);
        m_heap[heap_idx].first = gain;
        siftDown(heap_idx);
}

inline void maxNodeHeap::increaseKey(NodeID node, Gain gain) {
        ASSERT_TRUE(has(node));
        int queue_idx = index_of(node);
        int heap_idx  = m_elements[queue_idx].get_index();
        m_elements[queue_idx].set_key(gain);
        m_heap[heap_idx].first = gain;
        siftUp(heap_idx);
}

inline Gain maxNodeHeap::getKey(NodeID node) {
        return m_heap[m_elements[index_of(node)].get_index()].first;
};


inline bool maxNodeHeap::contains(NodeID node) {
       return has(node);
}

#endif
