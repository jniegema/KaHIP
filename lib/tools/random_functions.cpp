/******************************************************************************
 * random_functions.cpp 
 * *
 * Source of KaHIP -- Karlsruhe High Quality Partitioning.
 * Christian Schulz <christian.schulz.phone@gmail.com>
 *****************************************************************************/

#include "random_functions.h"

thread_local MersenneTwister random_functions::m_mt;
thread_local int random_functions::m_seed = 0;
thread_local MersenneTwister random_functions::m_doubles;
bool random_functions::m_thread_streams = false;

random_functions::random_functions()  {
}

random_functions::~random_functions() {
}
