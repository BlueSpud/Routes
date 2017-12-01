//
// Created by Logan on 10/12/17.
//

#include "population.h"

Population::Population(int pop_size, glm::vec4 start, glm::vec4 dest, const ElevationData& data) : _pop_size(pop_size), _start(start),
    _dest(dest), _direction(_dest - _start), _data(data) {

    // Figure out how many points we need for this route
    calcGenomeSize();

    // Figure out the number of vectors that make up the entire individual. This is the header, the start
    // and the destination
    _individual_size = _genome_size + 2 + 1;

    // Create the appropriate vectors
    _individuals = std::vector<glm::vec4>(pop_size * _individual_size);
    _opencl_individuals =  boost::compute::vector<glm::vec4>(_individuals.size(), Kernel::getContext());

    // Calculate the binomial coefficients for evaluating the bezier paths
    calcBinomialCoefficients();

    // Figure out how many points this route should be evaluated on.
    // We also make sure it is a multiple of workers
    glm::dvec2 cropped_size = data.getCroppedSizeMeters();
    _num_evaluation_points = ceil(glm::max(cropped_size.x / METERS_TO_POINT_CONVERSION,
                                           cropped_size.y / METERS_TO_POINT_CONVERSION) / 50.0) * 50;
    
    std::cout << "Using " << _num_evaluation_points << " points of evaluation" << std::endl;
    _num_evaluation_points_1 = (float)_num_evaluation_points - 1.0f;

    // First we init the params, then generate a starter population
    initParams();
    samplePopulation();

}

Individual Population::getIndividual(int index) {

    Individual ind;
    ind.num_genes = _genome_size;

    // Calculate the location of the parts of the individual
    glm::vec4* header_loc = _individuals.data() + index * _individual_size;
    ind.header = header_loc;

    // Account for the header
    ind.path = header_loc + 1;
    ind.genome = header_loc + 2;

    return ind;

}

void Population::step(const Pod& pod) {
    
    // Evaluate the cost and sort so the most fit solutions are in the front
    evaluateCost(pod);
    sortIndividuals();
    
    // Update the params
    updateParams();

    // Sample a new generation
    samplePopulation();
    
}

void Population::sortIndividuals() {

    // Get all of the individuals as Individuals for easier sorting
    std::vector<Individual> individuals_s = std::vector<Individual>(_pop_size);
    for (int i = 0; i < _pop_size; i++)
        individuals_s[i] = getIndividual(i);

    // Sort the array of individual structs
    std::sort(individuals_s.begin(), individuals_s.end(), [](Individual a, Individual b){

        // Compare costs in the header
        return (*a.header).x < (*b.header).x;

    });

    // Create a new vector so we don't destroy any data
    std::vector<glm::vec4> sorted_individuals = std::vector<glm::vec4>(_individuals.size());
    for (int i = 0; i < _pop_size; i++) {

        // Copy the sorted individual into the new array
        memcpy(sorted_individuals.data() + i * _individual_size, individuals_s[i].header, sizeof(glm::vec4) * _individual_size);

    }

    // Save the sorted array
    _individuals = sorted_individuals;

}

void Population::evaluateCost(const Pod& pod) {

    // Get stuff we need to execute a kernel on
    boost::compute::command_queue& queue = Kernel::getQueue();

    // Generate the program sounds
    static const std::string source = BOOST_COMPUTE_STRINGIZE_SOURCE(

        // Evaluates the bezier curve made by the given control points and start and dest at parametric value s
        float4 evaluateBezierCurve(__global float4* controls, int offset, int points, float s, __global int* binomial_coeffs) {

            float one_minus_s = 1.0 - s;

            // Degree is num points - 1
            int degree = points - 1;

            float4 out_point = (float4)(0.0, 0.0, 0.0, 0.0);

            // Middle terms, iterate for num points
            for (int i = 0; i < points; i++) {

                // Evaluate for x y and z
                float multiplier = pown(one_minus_s, degree - i) * pown(s, i);

                // We subtract one here so that we use the correct control point since i starts at 1
                out_point += controls[i + offset] * multiplier * (float)binomial_coeffs[i];

            }

            return out_point;

        }

        // This calculates the approximates curvature at a given point (p1)
        float curvature(float4 p0, float4 p1, float4 p2) {

            // Calculate the approximate first derivatives
            float4 der_first0 = p1 - p0;
            float4 der_first1 = p2 - p1;

            // Get the second derivative
            float4 der_second = der_first1 - der_first0;

            // Calculate the denominator and numerator
            float denom = length(cross(der_first0, der_second));
            float num = pown(length(der_first0), 3);

            return fabs(num / denom);

        }

        // Computes the cost of a path
        __kernel void cost(__read_only image2d_t image, __global float4* individuals, int path_length,
                           float max_grade_allowed, float min_curve_allowed, float excavation_depth, float width,
                           float height, __global int* binomial_coeffs,
                           float num_points_1, int points_per_worker, float origin_x, float origin_y) {

            const sampler_t sampler = CLK_NORMALIZED_COORDS_TRUE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;
            const float pylon_cost = 0.000116;
            const float tunnel_cost = 0.310;
            const float curve_weight = 300000.0;
            const float grade_weight = 100.0;

            __local float curve_sums [50];
//            __local float min_curves [50];
            __local float max_grades [50];
            __local float track_costs[50];

            // Get an offset to the gnome
            size_t i = get_global_id(0);
            size_t w = get_local_id(1);

            int path = i * (path_length + 1) + 1;

//            float min_curve = 10000000000000.0;
            float curve_sum = 0.0;

            float track_cost = 0.0;
            float steepest_grade = 0.0;

            // Figure out where to start and end
            int start = w * points_per_worker;
            int end = start + points_per_worker;

            float4 last_last = individuals[path];
            float4 last_point = individuals[path];

            // If we start at an offset, we need to make sure we "remember" the information that the last worker
            // calculated. The fastest way to do this is to just re-calculate it
            if (w) {

                last_last   = evaluateBezierCurve(individuals, path, path_length, (float)(start - 2) / num_points_1, binomial_coeffs);
                last_point  = evaluateBezierCurve(individuals, path, path_length, (float)(start - 1) / num_points_1, binomial_coeffs);

            }

            for (int p = start; p <= end; p++) {

                // Evaluate the bezier curve
                float4 bezier_point = evaluateBezierCurve(individuals, path, path_length, (float)p / num_points_1, binomial_coeffs);

                // Get the elevation of the terrain at this point. We do this with a texture sample
                float2 nrm_device = (float2)((bezier_point.x - origin_x) / width, (bezier_point.y - origin_y) / height);
                float height = read_imagef(image, sampler, nrm_device).x;

                // Compute spacing, only x and y distance, z delta is handled by the grade
                float spacing = sqrt(pown(bezier_point.x - last_point.x, 2) + pown(bezier_point.y - last_point.y, 2));

                // Get curvature
                if (p > 1) {

                    float curve = curvature(last_last, last_point, bezier_point);
                    curve_sum += 1.0 / curve * curve_weight;
//                    min_curve = min(min_curve, curve);

                }

                // Compute grade and track cost if there was spacing
                if (spacing) {

                    steepest_grade = max(steepest_grade, fabs(bezier_point.z - last_point.z) / spacing);

                    // Compute track cost
                    // First we get the pylon height which is the distance from the point on the track to the actual terrain
                    float pylon_height = bezier_point.z - height;

                    // Cost for the track being above the terrain. This is significantly less than if it was
                    // underground because no tunneling is needed
                    float above_cost = 0.5 * (fabs(pylon_height) + pylon_height);
                    above_cost = pown(above_cost, 2) * pylon_cost;

                    // Cost for the track being below the ground.
                    // For a delta of <= excavation_depth we don't count as tunneling because excavation will suffice
                    float below_cost = (-fabs(pylon_height + excavation_depth) + pylon_height + excavation_depth);
                    float below_cost_den = 2.0 * pylon_height + 2.0 * (excavation_depth);

                    below_cost = below_cost / below_cost_den * tunnel_cost;
                    track_cost += (above_cost + below_cost) * spacing;

            }

                // Keep track of the stuff that was computed so that we can calculate the delta at the next point
                last_last = last_point;
                last_point = bezier_point;

            }

            // Write to the buffer
            curve_sums [w] = curve_sum;
//            min_curves [w] = min_curve;
            max_grades [w] = steepest_grade;
            track_costs[w] = track_cost;

            barrier(CLK_LOCAL_MEM_FENCE);

            // This is only done on thread 0 so we don't do a million memory writes/reads
            if (!w) {

                // Figure out the final cost for everything. This would be equivalent to using one thread
                for (int m = 1; m < 50; m++) {

                     curve_sum += curve_sums[m];
//                     min_curve = min(min_curve, min_curves[m]);
                     steepest_grade = max(steepest_grade, max_grades[m]);
                     track_cost += track_costs[m];

                }

                // Now that we have all of the information about the route, we can calculate the cost.
                // Calculate grade cost, only apply a penalty if it is above 6%
                float grade_cost = grade_weight * (steepest_grade - max_grade_allowed + fabs(max_grade_allowed - steepest_grade)) + 1.0;

                // Calculate the curvature cost
                // Right now we are simply using the sum of curvature (not radius of curvature)
                float curve_cost = curve_sum;

                // Get total cost
                float total_cost = grade_cost + track_cost + curve_cost;

                // Set the individual's header to contain its cost
                individuals[path - 1].x = total_cost;

            }
    });

    // Get the data to allow for proper texture sampling
    glm::vec2 size_crop = _data.getCroppedSizeMeters();
    glm::vec2 origin = _data.getCroppedOriginMeters();

    // Create a temporary kernel and execute it
    static Kernel kernel = Kernel(source, "cost");
    kernel.setArgs(_data.getOpenCLImage(), _opencl_individuals.get_buffer(), _genome_size + 2,
                   MAX_SLOPE_GRADE, pod.minCurveRadius(), EXCAVATION_DEPTH, size_crop.x,
                   size_crop.y, _opencl_binomials.get_buffer(),
                   _num_evaluation_points_1, _num_evaluation_points / 50, origin.y, origin.y);

    // Upload the data
    boost::compute::copy(_individuals.begin(), _individuals.end(), _opencl_individuals.begin(), queue);
    
    // Execute the 2D kernel with a work size of 5. 5 threads working on a single individual
    kernel.execute2D(glm::vec<2, size_t>(0, 0),
                     glm::vec<2, size_t>(_pop_size, 50),
                     glm::vec<2, size_t>(1, 50));

    // Download the data
    boost::compute::copy(_opencl_individuals.begin(), _opencl_individuals.end(), _individuals.begin(), queue);

}

void Population::calcGenomeSize() {

    // The genome size has a square root relationship with the length of the route
    float sqrt_lenth = sqrt(glm::length(_direction));
    _genome_size = std::round(sqrt_lenth * LENGTH_TO_GENOME);
    std::cout << "Genome size: " << _genome_size << std::endl;

}

void Population::initParams() {
    
    // Calculate mu to be 1/4 of the total population
    _mu = _pop_size * 0.25;
    
    // Init the mean to the best guess (a straight line)
    bestGuess();
    
    // Calculate the weights for the selected population (mu)
    calcWeights();
    
    // Covariance matrix starts as the identity matrix
    // Multiply by three to make sure that we have room for X, Y and Z
    _covar_matrix = Eigen::MatrixXf::Identity(_genome_size * 3, _genome_size * 3);
    
    // Calculate the initial step size
    calcInitialSigma();
    
    // Calculate strat params
    calculateStratParameters();
    
    // Set the two evolution paths to the zero vector
    _p_covar = Eigen::VectorXf::Zero(_genome_size * 3);
    _p_sigma = Eigen::VectorXf::Zero(_genome_size * 3);
    
}

void Population::bestGuess() {
    
    // We choose a linear spacing of points along a straihgt line from the start to destination 
    _mean = Eigen::VectorXf(_genome_size * 3);
    
    for (int i = 1; i <= _genome_size; i++) {
    
        // Figure out how far along the direciton line we are
        float percent = (float)i / (float)(_genome_size + 1);
        
        // Set the mean
        int i_adjusted = (i - 1) * 3;
        _mean(i_adjusted    ) = _start.x + _direction.x * percent;
        _mean(i_adjusted + 1) = _start.y + _direction.y * percent;
        _mean(i_adjusted + 2) = _start.z + _direction.z * percent;
        
    }
    
}

void Population::calcWeights() {
    
    // When we update the mean and covariance matrix, we weight each solution unevenly.
    // Sum of all values in _weights should equal 1
    // Sum of 1/pow(_weights, 2) should be about _pop_size / 4
    _weights = std::vector<float>(_mu);
    float sum = 0.0;
    
    for (int i = 0; i < _mu; i++) {
        
        _weights[i] = log(_mu + 0.5) - log(i + 1);
        sum += _weights[i];
        
    }
    
    // Normalize to make sure that it adds up to 1
    for (int i = 0; i < _mu; i++)
        _weights[i] /= sum;
    
    sum = 0.0;
        for (int i = 0; i < _mu; i++)
            sum += _weights[i];
    
    // Calculate _mu_weight to be the sum of 1/poq(_weights, 2)
    sum = 0.0;
    
    for (int i = 0; i < _mu; i++)
        sum += glm::pow(_weights[i], 2.0f);
    
    _mu_weight = 1.0 / sum;
    
}

void Population::calcInitialSigma() {
    
    // Initialize sigma
    _sigma = Eigen::VectorXf(_genome_size * 3);
    
    // First we figure out what the actual values should be
    glm::vec3 sigma_parts = glm::vec3(INITAL_SIGMA_XY,
                                      INITAL_SIGMA_XY,
                                      (_data.getMaxElevation() - _data.getMinElevation()) / INITIAL_SIGMA_DIVISOR);
    
    // Apply it to the X Y and Z for each point
    for (int i = 0; i < _genome_size; i++) {
        
        _sigma(i * 3    ) = sigma_parts.x;
        _sigma(i * 3 + 1) = sigma_parts.y;
        _sigma(i * 3 + 2) = sigma_parts.z;
        
    }
    
}

void Population::calculateStratParameters() {
    
    // Calc c_sigma
    _c_sigma = (_mu_weight + 2.0) / (_genome_size + _mu_weight + 5.0);
    
    
}

void Population::samplePopulation() {
    
    // Create a MND
    MultiNormal dist = MultiNormal(_covar_matrix, _mean, _sigma);
    std::vector<Eigen::VectorXf> samples = dist.generateRandomSamples(_pop_size);
    
    // Convert the samples over to a set of vectors and update the population
    for (int i = 0; i < _pop_size; i++) {
        
        _individuals[i * _individual_size + 1] = _start;
        _individuals[i * _individual_size + 2 + _genome_size] = _dest;
        
        for (int j = 0; j < _genome_size; j++) {
            
            // Get a reference to the right gene in the genome of the ith individual
            glm::vec4& point = _individuals[i * _individual_size + 2 + j];
            
            // Set the values in the glm::vec4 to be correct
            point.x = samples[i](j * 3    );
            point.y = samples[i](j * 3 + 1);
            point.z = samples[i](j * 3 + 2);
            
        }
        
    }
    
}

void Population::updateParams() {
    
    // Update the mean
    updateMean();
    
    // Update the step size path
//    updatePSigma();
    
    
    // Update the step size
//    updateSigma();
    
}

void Population::updateMean() {
    
    // Save the mean from the last gen so we can use it to update the paths
    _mean_prime = _mean;
    
    _mean = Eigen::VectorXf::Zero(_genome_size * 3);
    
    for (int i = 0; i < _mu; i++) {
        
        // Figure out where the individual starts in
        int individual_start = i * _individual_size + 2;
        
        // Go through each point and add it to the correct place in the mean
        for (int p = 0; p < _genome_size; p++) {
            
            // Since the sum of all of the values in _weights adds to 1, we dont need to divde
            // the mean by _mu at the end.
            _mean(p * 3    ) += _individuals[individual_start + p].x * _weights[i];
            _mean(p * 3 + 1) += _individuals[individual_start + p].y * _weights[i];
            _mean(p * 3 + 2) += _individuals[individual_start + p].z * _weights[i];
            
        }
        
    }
    
}

void Population::updatePSigma() {
    
    // Calculate the discount factor and its complement
    float discount = 1.0 - _c_sigma;
    float discount_comp = sqrt(1.0 - (discount * discount));
    
    // Calculate the mean displacement
    Eigen::VectorXf mean_displacement(_genome_size * 3);
    mean_displacement = (_mean - _mean_prime).cwiseQuotient(_sigma);
    float sqrt_mew_weight = sqrt(_mu_weight);
    
    // Get the inverse square root of the covariance matrix
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXf> solver(_covar_matrix);
    Eigen::MatrixXf inv_sqrt_C(solver.operatorInverseSqrt());
    
    _p_sigma = discount * _p_sigma + discount_comp * sqrt_mew_weight * inv_sqrt_C * mean_displacement;
    
}

void Population::updateSigma() {
    
    // Calculate ratio between the decay and the dampening
    float ratio = _c_sigma / STEP_DAMPENING;
    
    // Get the expected value of the identity distribution in N dimensions
    float expected = sqrt(_genome_size * 3.0) * (1.0 - 1.0 / (_genome_size * 12.0) + 1.0 / (21.0 * glm::pow((_genome_size * 3.0), 2.0)));
    
    // Get the magnitude of the sigma path
    float mag = _sigma.norm();
    
    // Update sigma
    _sigma = _sigma * glm::pow(M_E, ratio * (mag / expected - 1.0));
    
}

void Population::calcBinomialCoefficients() {

    // For degree we have _genome_size + 2 points, so we use that minus 1 for the degree
    const std::vector<int>& binomials = Bezier::getBinomialCoefficients(_genome_size + 1);
    _opencl_binomials = boost::compute::vector<int>(_genome_size + 2, Kernel::getContext());

    // Upload to the GPU
    boost::compute::copy(binomials.begin(), binomials.end(), _opencl_binomials.begin(), Kernel::getQueue());

}

