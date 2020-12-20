#include "DAGTask.h"

void DAGTask::configureParams(){
    maxCondBranches = 2;
    maxParBranches  = 6;
    p_cond          = 0; 
    p_par           = 0.2;
    p_term          = 0.8;
    rec_depth       = 2;
    Cmin            = 1;
    Cmax            = 10;
    addProb         = 0.1;
    probSCond       = 0.5;

    weights.push_back(p_cond);
    weights.push_back(p_par);
    weights.push_back(p_term);
    dist.param(std::discrete_distribution<int> ::param_type(std::begin(weights), std::end(weights)));

    if(REPRODUCIBLE) gen.seed(1);
    else gen.seed(time(0));
    
}

void DAGTask::assignWCET(const int minC, const int maxC){
    for(auto &v: V)
        v->c = rand()%maxC + minC;
}

void DAGTask::expandTaskSeriesParallel(SubTask* source,SubTask* sink,const int depth,const int numBranches, const bool ifCond){

    int depthFactor = std::max(maxCondBranches, maxParBranches);
    int horSpace = std::pow(depthFactor,depth);

    if(source == nullptr && sink==nullptr){
        SubTask *so = new SubTask; //source
        SubTask *si = new SubTask; //sink
        so->depth = depth;
        si->depth = -depth;
        so->width = 0;
        so->width = 0;
        si->id = 1;

        V.push_back(so);
        V.push_back(si);

        double r = ((double) rand() / (RAND_MAX));
        if (r < probSCond){ //make it conditional
            int cond_branches = rand() % maxCondBranches + 2;
            expandTaskSeriesParallel(V[0], V[1], depth - 1, cond_branches, true);
        }
        else{
            int par_branches = rand() % maxParBranches + 2;
            expandTaskSeriesParallel(V[0], V[1], depth - 1, par_branches, false);
        }
    }
    else{
        float step = horSpace / (numBranches - 1);
        float w1 = (source->width - horSpace / 2);
        float w2 = (sink->width - horSpace / 2);

        for(int i=0; i<numBranches; ++i){
            
            // int current = V.size();
            creationStates state = TERMINAL_T;
            if (depth != 0) state = static_cast<creationStates>(dist(gen));

            switch (state){
            case TERMINAL_T:{
                SubTask *v = new SubTask;
                v->id = V.size();
                v->pred.push_back(source);
                v->succ.push_back(sink);
                v->mode = ifCond? C_INTERN_T : NORMAL_T;
                v->depth = depth;
                v->width = w1 + step * (i - 1);

                V.push_back(v);

                source->mode    = ifCond ? C_SOURCE_T : NORMAL_T;
                sink->mode      = ifCond ? C_SINK_T : NORMAL_T;
                source->succ.push_back(V[V.size()-1]);
                
                sink->pred.push_back(V[V.size()-1]);
                break;
            }
            case PARALLEL_T: case CONDITIONAL_T:{
                SubTask *v1 = new SubTask;
                v1->id = V.size();
                v1->pred.push_back(source);
                v1->mode = ifCond? C_INTERN_T : NORMAL_T;
                v1->depth = depth;
                v1->width = w1 + step * (i - 1);
                V.push_back(v1);

                source->succ.push_back(V[V.size()-1]);
                source->mode = ifCond ? C_SOURCE_T : NORMAL_T;

                SubTask *v2 = new SubTask;
                v2->id = V.size();
                v2->succ.push_back(sink);
                v2->mode = ifCond? C_INTERN_T : NORMAL_T;
                v2->depth = -depth;
                v2->width = w2 + step * (i - 1);
                V.push_back(v2);

                sink->pred.push_back(V[V.size()-1]);
                sink->mode = ifCond ? C_SINK_T : NORMAL_T;

                int max_branches = (state == PARALLEL_T )? maxParBranches : maxCondBranches;
                float cond = (state == PARALLEL_T) ? false: true;

                int branches = rand() % max_branches + 2;               
            
                expandTaskSeriesParallel(V[V.size()-2], V[V.size()-1], depth - 1, branches, cond);
            
                break;
            }
            
            default:
                break;
            }
        }
    }
}

void DAGTask::makeItDag(float prob){
    bool is_already_succ= false;
    std::vector<int> v_cond_pred;
    std::vector<int> w_cond_pred;
    for(auto &v:V){
        v_cond_pred  = v->getCondPred();

        for(auto &w:V){
            w_cond_pred = w->getCondPred();
            is_already_succ = false;

            isSuccessor(w, v, is_already_succ);

            if( v->depth > w->depth &&
                v->mode != C_SOURCE_T &&
                !is_already_succ &&
                w_cond_pred.size() == v_cond_pred.size() && 
                std::equal(v_cond_pred.begin(), v_cond_pred.end(), w_cond_pred.begin()) && 
                ((double) rand() / (RAND_MAX)) < prob
            )
            {
                //add an edge v -> w
                v->succ.push_back(w);
                w->pred.push_back(v);
            }
        }
    }
}

void DAGTask::computeWorstCaseWorkload(){
    // Algorithm 1, Melani et al. "Response-Time Analysis of Conditional DAG Tasks in Multiprocessor Systems"
    if(!ordIDs.size())
        topologicalSort();

    std::vector<std::set<int>> paths (V.size());
    paths[ordIDs[ordIDs.size()-1]].insert(ordIDs[ordIDs.size()-1]);
    int idx;
    for(int i = ordIDs.size()-2; i >= 0; --i ){
        idx = ordIDs[i];
        paths[idx].insert(idx);

        if(V[idx]->succ.size()){
            if(V[idx]->mode != C_SOURCE_T){
                for(int j=0; j<V[idx]->succ.size(); ++j)
                    paths[idx].insert(paths[V[idx]->succ[j]->id].begin(), paths[V[idx]->succ[j]->id].end());
            }
            else{
                std::vector<int> sum (V[idx]->succ.size(), 0);
                int max_id = 0;
                float max_sum = 0;
                for(int j=0; j<V[idx]->succ.size(); ++j){
                    for(auto k: paths[V[idx]->succ[j]->id])
                        sum[j] += V[k]->c;
                    if(sum[j] > max_sum){
                        max_sum = sum[j];
                        max_id = j;
                    }
                }
                paths[idx].insert(paths[V[idx]->succ[max_id]->id].begin(), paths[V[idx]->succ[max_id]->id].end());
            }
        }
    }

    wcw = 0;
    for(auto i:paths[ordIDs[0]]){
        wcw += V[i]->c;
    }
}


void DAGTask::assignSchedParametersUUniFast(const float U){
    t = std::ceil(wcw / U);
    d = t;    
}
