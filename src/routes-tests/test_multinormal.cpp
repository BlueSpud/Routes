//
//  test_multinormal.cpp
//  Routes
//
//  Created by Logan on 11/17/17.
//

#include <normal/multinormal.h>
#include <boost/test/unit_test.hpp>

#define N 100000

BOOST_AUTO_TEST_CASE(test_multinormal) {
    
    // Create a multivariate normal distribution.
    // We then check to make sure the output has the same covariance
    Eigen::Matrix3f covariance;
    covariance << 2.0f, 1.0f, 0.5f,
                  1.0f, 2.0f, 1.0f,
                  0.5f, 1.0f, 2.0f;

    // Make the objects to sample from
    SampleGenerator* gen = new SampleGenerator(3, N);
    MultiNormal normal = MultiNormal(covariance, Eigen::Vector3f(2.0, 2.0, 2.0));
    
    // Generate 10000 _samples
    std::vector<Eigen::VectorXf> samples(N);

    // Make sure they are initialized
    for (int i = 0; i < N; i++)
        samples[i] = Eigen::VectorXf::Zero(3);

    normal.generateRandomSamples(samples, *gen);
    
    // Make sure that we have a distribution that is like the one we supplied
    Eigen::Vector3f m_prime;
    Eigen::Matrix3f covariance_prime;
    
    m_prime.setZero();
    covariance_prime.setZero();
    
    // Sum all _samples
    for (int i = 0; i < N; i++)
        m_prime += samples[i];
    
    // Divide
    m_prime /= N;
    
    // We give it 5% because its random
    BOOST_CHECK(fabs(m_prime(0)) < 0.1f);
    BOOST_CHECK(fabs(m_prime(1)) < 0.1f);
    BOOST_CHECK(fabs(m_prime(2)) < 0.1f);
    
    // Compute covariance matrix, this is a little less trivial
     for (int i = 0; i < N; i++)
         covariance_prime += (samples[i] - m_prime) * (samples[i] - m_prime).transpose();
    
    covariance_prime /= N;
    
    BOOST_CHECK_CLOSE(covariance_prime(0, 0), 8.0f, 5);
    BOOST_CHECK_CLOSE(covariance_prime(0, 1), 4.0f, 5);
    BOOST_CHECK_CLOSE(covariance_prime(0, 2), 2.0f, 5);
    
    BOOST_CHECK_CLOSE(covariance_prime(1, 0), 4.0f, 5);
    BOOST_CHECK_CLOSE(covariance_prime(1, 1), 8.0f, 5);
    BOOST_CHECK_CLOSE(covariance_prime(1, 2), 4.0f, 5);
    
    BOOST_CHECK_CLOSE(covariance_prime(2, 0), 2.0f, 5);
    BOOST_CHECK_CLOSE(covariance_prime(2, 1), 4.0f, 5);
    BOOST_CHECK_CLOSE(covariance_prime(2, 2), 8.0f, 5);

    // Clean up
    delete gen;
    
}
