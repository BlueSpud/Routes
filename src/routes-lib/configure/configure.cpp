//
// Created by isaac on 8/14/18.
//

#include "configure.h"

Configuration Configure::_config;

Configure::Configure() {

    loadConfig();

}

void Configure::loadConfig() {


    boost::property_tree::ptree root;

    boost::property_tree::read_json(FILE_PATH, root);

    // if 0, then reload, if 1 then don't
    int reload = root.get<int>("reload", 0);

    int population_size = root.get<int>("routes.population-size", 0);
    int num_generations = root.get<int>("routes.num-generations", 0);
    float initial_sigma_divisor = root.get<float>("population.initial-sigma-divisor", 0);
    float initial_sigma_xy = root.get<float>("population.initial-sigma-xy", 0);
    float step_dampening = root.get<float>("population.step-dampening", 0);
    float alpha = root.get<float>("population.alpha", 0);
    int num_sample_threads = root.get<int>("population.num-sample-threads", 0);
    int num_route_workers = root.get<int>("population.num-route-workers", 0);

    _config = {reload, population_size, num_generations,
               initial_sigma_divisor, initial_sigma_xy,
               step_dampening, alpha, num_sample_threads, num_route_workers};



}

int Configure::getReload() {
    return _config.reload;
}

int Configure::getPopulationSize() {
    return _config.population_size;
}

int Configure::getNumGenerations() {
    return _config.num_generations;
}

float Configure::getInitialSigmaDivisor() {
    return _config.initial_sigma_divisor;
}

float Configure::getInitialSigmaXY() {
    return _config.initial_sigma_xy;
}

float Configure::getStepDampening() {
    return _config.step_dampening;
}

float Configure::getAlpha() {
    return _config.alpha;
}

int Configure::getNumSampleThreads() {
    return _config.num_sample_threads;
}

int Configure::getNumRouteWorkers() {
    return _config.num_route_workers;
}

