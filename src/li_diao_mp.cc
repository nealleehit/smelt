#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>
#include <ctime>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
// Boost random generator
#include <boost/random/normal_distribution.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_real_distribution.hpp>
#include <boost/random/variate_generator.hpp>
// Eigen dense matrices
#include <Eigen/Dense>

#include "beta_dist.h"
#include "li_diao_mp.h"
#include "factory.h"
#include "function_dispatcher.h"
#include "json_object.h"
#include "nelder_mead.h"
#include "normal_dist.h"
#include "normal_multivar.h"
#include "numeric_utils.h"

stochastic::LiningDiaozemin_MP::LiningDiaozemin_MP(
    stochastic::FaultType faulting, stochastic::SimulationType simulation_type,
    double moment_magnitude, double depth_to_rupt, double rupture_distance,
    double vs30, double s_or_d, unsigned int num_sims,
    unsigned int num_realizations, bool truncate, int seed_value,
    int pos, stochastic::CohType coh_type)
    : StochasticModel(),
      faulting_{faulting},
      sim_type_{simulation_type},
      moment_magnitude_{moment_magnitude},
      depth_to_rupt_{depth_to_rupt},
      rupture_dist_{rupture_distance},
      vs30_{vs30},
      s_or_d_{s_or_d},
      truncate_{truncate},
      num_realizations_{num_realizations},
      seed_value_{seed_value},
      pos_ {pos},
      coh_type_ {coh_type},
      time_step_{0.005}
{
  model_name_ = "LiningDiaozemin_MP";

  switch (sim_type_) {
    case stochastic::SimulationType::NoPulse:
      num_sims_pulse_ = 0;
      break;

    case stochastic::SimulationType::Pulse:
      num_sims_pulse_ = num_sims;
      break;

    case stochastic::SimulationType::PulseAndNoPulse:
      num_sims_pulse_ = simulate_pulse_type(num_sims);
      break;
  }
  
  switch (coh_type_) {
    case stochastic::CohType::FengHu:
      //;
      break;

    case stochastic::CohType::HarichandranVanmarcke:
      //num_sims_pulse_ = num_sims;
      break;

    case stochastic::CohType::LohYeh:
      //num_sims_pulse_ = simulate_pulse_type(num_sims);
      break;
    case stochastic::CohType::QuTJ:
      // num_sims_pulse_ = simulate_pulse_type(num_sims);
      break;
    case stochastic::CohType::HaoH:
      // num_sims_pulse_ = simulate_pulse_type(num_sims);
      break;
    case stochastic::CohType::Nakamura:
      // num_sims_pulse_ = simulate_pulse_type(num_sims);
      break;
  }
  num_sims_nopulse_ = num_sims - num_sims_pulse_;

  // Initialize multivariate normal generator without seed
  sample_generator_ =
      Factory<numeric_utils::RandomGenerator, int>::instance()->create(
          "MultivariateNormal", std::move(seed_value_));
  
  // Set regression constants
  std_dev_pulse_.resize(19);
  std_dev_nopulse_.resize(14);
  corr_matrix_pulse_.resize(19, 19);
  corr_matrix_nopulse_.resize(14, 14);
  beta_distribution_pulse_.resize(19, 8);
  beta_distribution_nopulse_.resize(14, 8);
  params_lower_bound_.resize(19);
  params_upper_bound_.resize(19);
  params_fitted1_.resize(19);
  params_fitted2_.resize(19);
  params_fitted3_.resize(19);  

  // clang-format off
  std_dev_pulse_ <<
      0.406, 0.527 , 1.0,
      1.0, 0.384 , 0.721 ,
      0.315 , 0.408 , 0.349 ,
      0.367 , 0.659 , 1.004 ,
      0.879 , 0.372 , 0.665 ,
      0.442 , 0.404 , 0.852 ,
      0.908;
      
  std_dev_nopulse_ <<
      1.052723262620090, 0.398427668080412, 0.456828618083131,
      0.305727090125879, 0.447517114210032, 0.941288665677244,
      1.007680943597980, 1.028226030318770, 0.375877126809162,
      0.458413470215522, 0.294118965466636, 0.399966941388161,
      0.831550984874095, 0.887870513796394;

  corr_matrix_pulse_ <<
 1.00,	-0.10,	-0.06,	0.06,	0.03,	0.59,	0.02,	0.01,	0.03,	-0.32,	0.14,	0.00,	0.56,	-0.10,	0.06,	0.00,	-0.15,	0.11,	0.03,
-0.10,	1.00,	0.22,	-0.06,	0.82,	-0.12,	0.67,	0.77,	0.81,	-0.21,	0.30,	0.31,	-0.03,	0.60,	0.77,	0.75,	-0.10,	0.31,	0.26,
-0.06,	0.22,	1.00,	-0.13,	0.23,	0.12,	0.18,	0.18,	0.21,	-0.19,	0.14,	0.13,	0.11,	0.03,	0.20,	0.20,	0.01,	0.09,	-0.10,
0.06,	-0.06,	-0.13,	1.00,	-0.02,	-0.05,	-0.02,	-0.06,	-0.04,	-0.01,	-0.14,	0.01,	-0.10,	-0.03,	-0.03,	-0.06,	-0.03,	-0.05,	-0.02,
0.03,	0.82,	0.23,	-0.02,	1.00,	-0.12,	0.75,	0.92,	0.95,	-0.26,	0.23,	0.30,	-0.08,	0.70,	0.90,	0.92,	-0.21,	0.30,	0.25,
0.59,	-0.12,	0.12,	-0.05,	-0.12,	1.00,	-0.12,	-0.12,	-0.08,	-0.02,	0.22,	-0.01,	0.76,	-0.15,	-0.02,	-0.05,	0.08,	0.07,	-0.15,
0.02,	0.67,	0.18,	-0.02,	0.75,	-0.12,	1.00,	0.67,	0.76,	-0.34,	0.20,	0.20,	-0.14,	0.83,	0.69,	0.73,	-0.18,	0.25,	0.19,
0.01,	0.77,	0.18,	-0.06,	0.92,	-0.12,	0.67,	1.00,	0.96,	-0.29,	0.31,	0.30,	-0.12,	0.71,	0.89,	0.93,	-0.22,	0.34,	0.30,
0.03,	0.81,	0.21,	-0.04,	0.95,	-0.08,	0.76,	0.96,	1.00,	-0.26,	0.31,	0.32,	-0.07,	0.75,	0.92,	0.95,	-0.20,	0.37,	0.26,
-0.32,	-0.21,	-0.19,	-0.01,	-0.26,	-0.02,	-0.34,	-0.29,	-0.26,	1.00,	-0.35,	0.08,	0.19,	-0.27,	-0.30,	-0.29,	0.59,	-0.11,	-0.11,
0.14,	0.30,	0.14,	-0.14,	0.23,	0.22,	0.20,	0.31,	0.31,	-0.35,	1.00,	0.11,	0.10,	0.22,	0.34,	0.32,	-0.16,	0.54,	0.25,
0.00,	0.31,	0.13,	0.01,	0.30,	-0.01,	0.20,	0.30,	0.32,	0.08,	0.11,	1.00,	0.06,	0.21,	0.26,	0.29,	0.06,	0.20,	0.75,
0.56,	-0.03,	0.11,	-0.10,	-0.08,	0.76,	-0.14,	-0.12,	-0.07,	0.19,	0.10,	0.06,	1.00,	-0.34,	-0.03,	-0.12,	0.29,	0.05,	-0.28,
-0.10,	0.60,	0.03,	-0.03,	0.70,	-0.15,	0.83,	0.71,	0.75,	-0.27,	0.22,	0.21,	-0.34,	1.00,	0.66,	0.77,	-0.26,	0.20,	0.29,
0.06,	0.77,	0.20,	-0.03,	0.90,	-0.02,	0.69,	0.89,	0.92,	-0.30,	0.34,	0.26,	-0.03,	0.66,	1.00,	0.95,	-0.22,	0.34,	0.24,
0.00,	0.75,	0.20,	-0.06,	0.92,	-0.05,	0.73,	0.93,	0.95,	-0.29,	0.32,	0.29,	-0.12,	0.77,	0.95,	1.00,	-0.25,	0.34,	0.27,
-0.15,	-0.10,	0.01,	-0.03,	-0.21,	0.08,	-0.18,	-0.22,	-0.20,	0.59,	-0.16,	0.06,	0.29,	-0.26,	-0.22,	-0.25,	1.00,	-0.27,	-0.33,
0.11,	0.31,	0.09,	-0.05,	0.30,	0.07,	0.25,	0.35,	0.37,	-0.11,	0.54,	0.20,	0.05,	0.20,	0.34,	0.34,	-0.27,	1.00,	0.23,
0.03,	0.26,	-0.10,	-0.02,	0.25,	-0.15,	0.19,	0.30,	0.26,	-0.11,	0.25,	0.75,	-0.28,	0.29,	0.24,	0.27,	-0.33,	0.23,	1.00;

  corr_matrix_nopulse_ <<
      1, -0.183620641202513, 0.0890171218487119, 0.104132896092390, 0.0143281984142704, 0.202871723469377, -0.151909317725644, 0.945163870283100, -0.0778432911362303, 0.0495683691288216, 0.0966843496273208, 0.0917721771113965, 0.103741261286614, -0.121195511065596,
      -0.183620641202513, 1, 0.0854423655718587, 0.307373593686928, -0.0150490152853094, -0.149397471797000, 0.0888348816281089, -0.0794047082384602, 0.848433024094089, 0.0950830201555768, 0.288741920713302, -0.0596971902001111, -0.0160895956989375, 0.113905908782625,
      0.0890171218487119, 0.0854423655718587, 1, 0.813213357087557, -0.225438606252670, 0.000735703328121357, -0.0840944291284059, 0.0562899783045387, 0.137192754102300, 0.907605550316775, 0.788263163970441, -0.189167012249831, -0.0219422025252862, -0.0931560965857281,
      0.104132896092390, 0.307373593686928, 0.813213357087557, 1, -0.162877782549727, -0.0946212742252604, -0.0192432432422716, 0.131014027317413, 0.289495591671556, 0.752836252137069, 0.908489822222397, -0.151488045845577, -0.0637984858287899, -0.0498615936932623,
      0.0143281984142704, -0.0150490152853094, -0.225438606252670, -0.162877782549727, 1, -0.187527904866745, -0.163661457269968, 0.0720174301613615, -0.0805658506819631, -0.173027594836660, -0.165135405413428, 0.897082085156820, -0.0778400029130002, -0.00420375269007833,
      0.202871723469377, -0.149397471797000, 0.000735703328121357, -0.0946212742252604, -0.187527904866745, 1, -0.0853760806838177, 0.151981779909482, -0.0282514238500333, 0.00217240817323823, -0.0879254146125414, -0.0874961774514421, 0.647468312996265, -0.157070775414507,
      -0.151909317725644, 0.0888348816281089, -0.0840944291284059, -0.0192432432422716, -0.163661457269968, -0.0853760806838177, 1, -0.109821275189057, 0.0605607584025393, -0.0674419385042876, -0.0178734643092523, -0.0879585887923949, -0.105931812299965, 0.761324011431321,
      0.945163870283100, -0.0794047082384602, 0.0562899783045387, 0.131014027317413, 0.0720174301613615, 0.151981779909482, -0.109821275189057, 1, -0.0744735914486929, 0.0477307773362732, 0.116976058986177, 0.104731056969540, 0.137507782979518, -0.107158677162180,
      -0.0778432911362303, 0.848433024094089, 0.137192754102300, 0.289495591671556, -0.0805658506819631, -0.0282514238500333, 0.0605607584025393, -0.0744735914486929, 1, 0.0782297693189997, 0.294723991917604, -0.0895932216480470, -0.0501878780366773, 0.0967842822751738,
      0.0495683691288216, 0.0950830201555768, 0.907605550316775, 0.752836252137069, -0.173027594836660, 0.00217240817323823, -0.0674419385042876, 0.0477307773362732, 0.0782297693189997, 1, 0.786122088469745, -0.177521125226899, 0.00868592321024064, -0.0671981184080449,
      0.0966843496273208, 0.288741920713302, 0.788263163970441, 0.908489822222397, -0.165135405413428, -0.0879254146125414, -0.0178734643092523, 0.116976058986177, 0.294723991917604, 0.786122088469745, 1, -0.168356869639510, -0.0773913106180435, -0.0274804568206274,
      0.0917721771113965, -0.0596971902001111, -0.189167012249831, -0.151488045845577, 0.897082085156820, -0.0874961774514421, -0.0879585887923949, 0.104731056969540, -0.0895932216480470, -0.177521125226899, -0.168356869639510, 1, -0.183773515755380, 0.00693211686662153,
      0.103741261286614, -0.0160895956989375, -0.0219422025252862, -0.0637984858287899, -0.0778400029130002, 0.647468312996265, -0.105931812299965, 0.137507782979518, -0.0501878780366773, 0.00868592321024064, -0.0773913106180435, -0.183773515755380, 1, -0.110874872058067,
      -0.121195511065596, 0.113905908782625, -0.0931560965857281, -0.0498615936932623, -0.00420375269007833, -0.157070775414507, 0.761324011431321, -0.107158677162180, 0.0967842822751738, -0.0671981184080449, -0.0274804568206274, 0.00693211686662153, -0.110874872058067, 1;

  beta_distribution_pulse_ <<
    3.044, 0.476, -0.383, -0.633, 0, 0.229, -0.177,	0.009,
   -13.570,	2.272, 0, 4.523, -0.609, -0.517, -0.295, 0.007,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -7.897, 1.486, 0, 2.644, -0.319, -0.461, -0.262, 0.012,
    -2.372,	1.425, -1.389, -0.901, 0, 0.405, 0.000,	0,
    -3.865,	1.170, 0.615, 3.095, -0.406, -0.162, -0.428, 0,
    -4.321,	0.904, 0, 0.432, 0,	-0.368,	-0.217,	0,
    -4.300,	0.943, 0, 0.377, 0,	-0.384,	-0.173,	0,
    -2.348,	0.333, -0.858, 0, 0, -0.167, 0.315,	0,
    -2.956, 0.455, 0, 0, 0, 0, 0, 0,
    -3.313,	0.489, 0, 0, 0, 0, 0, 0,
    -9.383,	2.587, -3.110, -1.180, 0, 0, 0, 0,
    -3.107,	0.826, 0.974, 2.903, -0.358, 0.000, -0.264,	0,
    -8.330,	1.459, 0, 0.464, 0,	-0.515,	-0.274,	0.000,
    -7.058,	1.211, 0, 0.520, 0,	-0.427,	-0.117,	0.000,
    -2.156,	0.795, -1.442, 0, 0, -0.209, -0.088, 0.000,
    -4.505,	0.615, 0, 0, 0,	0, 0, 0,
    -2.956,	0.455, 0, 0, 0,	0, 0, 0;
    
  beta_distribution_nopulse_ <<
    8.09695881287823, 1.00609515629221, -1.39347614723327, -4.85869770683701, 0.472644100309933, 0.434550762616159, -0.862562872197509, 0,
    -1.03473761679032, 0.769091178587874, 0, 0.412237308297152, 0, -0.377739650769220, -0.424234099315427, 0,
    -4.72728279119446, 0.709717476708319, 0, 0.470974168011549, 0, -0.123518047425648, 0, 0,
    -4.44400222195478, 0.798093247074753, 0, 0.345405210060350, 0, -0.230823340141895, 0, 0,
    0.247133528936450, -0.149209862203390, 0, 0, 0, 0, 0.377202902904920, 0,
    -1.44302935447839, 0.223053706671624, 0, 0, 0, 0, 0, 0,
    -0.380413278316438, 0.159342468070527, 0, -0.298208438215333, 0, 0, 0, 0,
    7.30682757526241, 0.999256668956432, -1.33082594407524, -4.95306361630276, 0.490554994733579, 0.442502068793772, -0.835310070621911, 0,
    -0.403711730133755, 0.672375321924977, 0, 0.335372498461681, 0, -0.330322239630250, -0.366700025738387, 0,
    -4.79820204505010, 0.709160958437296, 0, 0.472560804537015, 0, -0.0755764830052928, 0, 0,
    -4.35041760661412, 0.785290791385159, 0, 0.325462132630085, 0, -0.221525656800750, 0, 0,
    0.424849811725595, -0.181204207590470, 0, 0, 0, 0, 0.401549107903204, 0,
    -2.97911606394595, 0.420016455603546, 0, 0, 0, 0, 0, 0,
    -0.703694160589291, 0.160571013696218, 0, -0.145792047865653, 0, 0, 0, 0;

  params_lower_bound_ << 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, -2, -4.7, 0, 0, 0, 0, 0, -2, -4.7;

  params_upper_bound_ << 0, 0, 3.2, 2, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0;
  
  params_fitted1_ << 0, 0, 1.26, 0, 0, 0, 0, 0, 0, 0, 15.85, 5.32, 0, 0, 0, 0, 0, 15.85, 5.32;
  
  params_fitted2_ << 0, 0, 4.05, 0, 0, 0, 0, 0, 0, 0, 3.98, 3.2, 0, 0, 0, 0, 0, 3.98, 3.2;

  params_fitted3_ << 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3.11, 0, 0, 0, 0, 0, 0, 3.11, 0;
  // clang-format on
}

utilities::JsonObject stochastic::LiningDiaozemin_MP::generate(
    const std::string& event_name, bool units) {

  // Create vectors for pulse-like and non-pulse-like motions
  std::vector<std::vector<std::vector<double>>> pulse_motions_comp1(
      num_sims_pulse_, std::vector<std::vector<double>>(num_realizations_,
                                                        std::vector<double>()));

  std::vector<std::vector<std::vector<double>>> pulse_motions_comp2(
      num_sims_pulse_, std::vector<std::vector<double>>(num_realizations_,
                                                        std::vector<double>()));

  std::vector<std::vector<std::vector<double>>> nopulse_motions_comp1(
      num_sims_nopulse_, std::vector<std::vector<double>>(
                             num_realizations_, std::vector<double>()));

  std::vector<std::vector<std::vector<double>>> nopulse_motions_comp2(
      num_sims_nopulse_, std::vector<std::vector<double>>(
                             num_realizations_, std::vector<double>()));

  // Generated simulated acceleration time histories
  try {
    // Simulate model parameters
    Eigen::MatrixXd parameters_pulse =
        simulate_model_parameters(true, num_sims_pulse_);
    Eigen::MatrixXd parameters_nopulse =
        simulate_model_parameters(false, num_sims_nopulse_);

    // Simulate pulse-like motions
    for (unsigned int i = 0; i < num_sims_pulse_; ++i) {
        simulate_near_fault_ground_motion(
            true, parameters_pulse.row(i), pulse_motions_comp1[i],
            pulse_motions_comp2[i], num_realizations_);
    } 

    // Simulate non-pulse-like motions
    for (unsigned int i = 0; i < num_sims_nopulse_; ++i) {
      simulate_near_fault_ground_motion(
          false, parameters_nopulse.row(i), nopulse_motions_comp1[i],
          nopulse_motions_comp2[i], num_realizations_);
    }

    // If requested, truncate and baseline correct time histories
    double gfactor = 981;
    unsigned int fit_order = 5;
    if (truncate_) {
      // First truncate motions
      for (unsigned int i = 0; i < num_sims_pulse_; ++i) {
        truncate_time_histories(pulse_motions_comp1[i], pulse_motions_comp2[i],
                                gfactor);
      }

      for (unsigned int i = 0; i < num_sims_nopulse_; ++i) {
        truncate_time_histories(nopulse_motions_comp1[i],
                                nopulse_motions_comp2[i], gfactor);
      }

      // Baseline correct truncated pulse-like motions
      for (unsigned int i = 0; i < num_sims_pulse_; ++i) {
        for (unsigned int j = 0; j < num_realizations_; ++j) {
          baseline_correct_time_history(pulse_motions_comp1[i][j], gfactor,
                                        fit_order);
          baseline_correct_time_history(pulse_motions_comp2[i][j], gfactor,
                                        fit_order);
        }
      }

      // Baseline correct trunacted non-pulse-like motions
      for (unsigned int i = 0; i < num_sims_nopulse_; ++i) {
        for (unsigned int j = 0; j < num_realizations_; ++j) {
          baseline_correct_time_history(nopulse_motions_comp1[i][j], gfactor,
                                        fit_order);
          baseline_correct_time_history(nopulse_motions_comp2[i][j], gfactor,
                                        fit_order);
        }
      }
    }
  } catch (const std::exception& e) {
    std::cerr << e.what();
    throw;
  }

  // Create JsonObject for events
  auto events = utilities::JsonObject();
  std::vector<utilities::JsonObject> events_array(
      num_realizations_ * (num_sims_pulse_ + num_sims_nopulse_));

  // Add pattern information for JSON
  auto pattern_x = utilities::JsonObject();
  auto pattern_z = utilities::JsonObject();  
  pattern_x.add_value("type", "UniformAcceleration");
  pattern_x.add_value("timeSeries", "accel_x");
  pattern_x.add_value("dof", 1);
  pattern_z.add_value("type", "UniformAcceleration");
  pattern_z.add_value("timeSeries", "accel_z");
  pattern_z.add_value("dof", 3);

  // Create JSON for specific event
  auto event_data = utilities::JsonObject();
  // Loop over simulations for different parameter sets for pulse-like
  // motions
  for (unsigned int i = 0; i < num_sims_pulse_; ++i) {
    // Loop over number of realizations per parameter set realization    
    for (unsigned int j = 0; j < num_realizations_; ++j) {
      event_data.add_value("name", event_name + "_ParameterSetPulse" + std::to_string(i) +
                                       "_Sim" + std::to_string(j));
      event_data.add_value("type", "Seismic");
      event_data.add_value("dT", time_step_);
      event_data.add_value("numSteps", pulse_motions_comp1[i][j].size());
      event_data.add_value(
          "pattern", std::vector<utilities::JsonObject>{pattern_x, pattern_z});

      // Rotate accelerations, if necessary      
      std::vector<double> x_accels(pulse_motions_comp1[i][j].size());
      std::vector<double> z_accels(pulse_motions_comp2[i][j].size());
      convert_time_history_units(pulse_motions_comp1[i][j], units);
      convert_time_history_units(pulse_motions_comp2[i][j], units);

      // Add time histories for x and z directions to event
      auto time_history_x = utilities::JsonObject();
      auto time_history_z = utilities::JsonObject();
      time_history_x.add_value("name", "accel_x");
      time_history_x.add_value("type", "Value");
      time_history_x.add_value("dT", time_step_);
      time_history_x.add_value("data", pulse_motions_comp1[i][j]);
      time_history_z.add_value("name", "accel_z");
      time_history_z.add_value("type", "Value");
      time_history_z.add_value("dT", time_step_);
      time_history_z.add_value("data", pulse_motions_comp2[i][j]);
      event_data.add_value("timeSeries", std::vector<utilities::JsonObject>{
                                             time_history_x, time_history_z});
      events_array[i * num_realizations_ + j] = event_data;
      event_data.clear();
    }
  }
  
  // Loop over different simulations for parameter sets non-pulse-like
  // motions
  for (unsigned int i = 0; i < num_sims_nopulse_; ++i) {
    // Loop over number of realizations per parameter set realization
    for (unsigned int j = 0; j < num_realizations_; ++j) {
      event_data.add_value("name", event_name + "_ParameterSetNoPulse" + std::to_string(i) +
                                       "_Sim" + std::to_string(j + num_sims_pulse_));
      event_data.add_value("type", "Seismic");
      event_data.add_value("dT", time_step_);
      event_data.add_value("numSteps", nopulse_motions_comp1[i][j].size());
      event_data.add_value(
          "pattern", std::vector<utilities::JsonObject>{pattern_x, pattern_z});

      // Rotate accelerations, if necessary
      std::vector<double> x_accels(nopulse_motions_comp1[i][j].size());
      std::vector<double> z_accels(nopulse_motions_comp2[i][j].size());
      convert_time_history_units(nopulse_motions_comp1[i][j], units);
      convert_time_history_units(nopulse_motions_comp2[i][j], units);

      // Add time histories for x and y directions to event
      auto time_history_x = utilities::JsonObject();
      auto time_history_z = utilities::JsonObject();
      time_history_x.add_value("name", "accel_x");
      time_history_x.add_value("type", "Value");
      time_history_x.add_value("dT", time_step_);
      time_history_x.add_value("data", nopulse_motions_comp1[i][j]);
      time_history_z.add_value("name", "accel_z");
      time_history_z.add_value("type", "Value");
      time_history_z.add_value("dT", time_step_);
      time_history_z.add_value("data", nopulse_motions_comp2[i][j]);
      event_data.add_value("timeSeries", std::vector<utilities::JsonObject>{
                                             time_history_x, time_history_z});
      events_array[i * num_realizations_ + j +
                   num_realizations_ * num_sims_pulse_] = event_data;
      event_data.clear();
    }
  }

  events.add_value("Events", events_array);

  return events;
}

bool stochastic::LiningDiaozemin_MP::generate(
    const std::string& event_name, const std::string& output_location,
    bool units) {
  bool status = true;
  
  // Generate pool of acceleration time histories
  try{
    auto json_output = generate(event_name, units);
    json_output.write_to_file(output_location);
  } catch (const std::exception& e) {
    std::cerr << e.what();
    status = false;
    throw;
  }

  return status;  
}

unsigned int stochastic::LiningDiaozemin_MP::simulate_pulse_type(
    unsigned int num_sims) const {
  double pulse_probability = 0.0;

  // Calculate pulse probability for any type of pulse
  if (faulting_ == stochastic::FaultType::StrikeSlip) {
    pulse_probability = 1.0 / (1.0 + std::exp(0.457 + 0.126 * rupture_dist_ -
                              0.244 * std::sqrt(s_or_d_) +
                              0.013 * 0.0)); //0.013 * theta_or_phi_));
  } else {
    pulse_probability = 1.0 / (1.0 + std::exp(0.304 + 0.072 * rupture_dist_ -
                              0.208 * std::sqrt(s_or_d_) +
                              0.021 * 0.0));   // 0.021 * theta_or_phi_));
  }

  // Create random generator for uniform distribution between 0.0 and 1.0
  auto generator =
      seed_value_ != std::numeric_limits<int>::infinity()
          ? boost::random::mt19937(static_cast<unsigned int>(seed_value_))
          : boost::random::mt19937(
                static_cast<unsigned int>(std::time(nullptr)));

  boost::random::uniform_real_distribution<> distribution(0.0, 1.0);
  boost::random::variate_generator<boost::random::mt19937&,
                                   boost::random::uniform_real_distribution<>>
      pulse_gen(generator, distribution);

  unsigned int number_of_pulses = 0;

  for (unsigned int i = 0; i < num_sims; ++i) {
    if (pulse_gen() < pulse_probability) {
      number_of_pulses++;
    }
  }

  return number_of_pulses;
}

Eigen::MatrixXd stochastic::LiningDiaozemin_MP::simulate_model_parameters(
    bool pulse_like, unsigned int num_sims) {
  // Calculate covariance matrix
  Eigen::MatrixXd error_cov =
      pulse_like
          ? numeric_utils::corr_to_cov(corr_matrix_pulse_, std_dev_pulse_)
          : numeric_utils::corr_to_cov(corr_matrix_nopulse_, std_dev_nopulse_);

  Eigen::MatrixXd simulated_params = pulse_like
                                         ? Eigen::MatrixXd::Zero(num_sims, 19)
                                         : Eigen::MatrixXd::Zero(num_sims, 14);

  Eigen::VectorXd error_mean =
      pulse_like ? Eigen::VectorXd::Zero(19) : Eigen::VectorXd::Zero(14);

  // Compute conditional mean values of transformed model parameters using
  // regression coefficients
  Eigen::VectorXd predicted_model_params =
      compute_transformed_model_parameters(pulse_like);

  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> parameter_realizations;
  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> epsilon;

  // Create simulated model parameters for specified number of motions
  double test;
  Eigen::VectorXd model_params(error_mean.size());

  // Loop over number of simulations requested, generating parameter realizations
  for (unsigned int i = 0; i < num_sims; ++i) {
    test = -1.0;

    // Continue looping in event parameters for pulse-like motion are unsatisfactory
    while (test < 0.0) {
      sample_generator_->generate(parameter_realizations, error_mean, error_cov,
                                  1);
      epsilon = pulse_like
                    ? parameter_realizations.cwiseQuotient(std_dev_pulse_)
                    : parameter_realizations.cwiseQuotient(std_dev_nopulse_);
      double max_epsilon = epsilon.cwiseAbs().maxCoeff();

      while (max_epsilon > 2.0) {
        sample_generator_->generate(parameter_realizations, error_mean,
                                    error_cov, 1);
        epsilon = pulse_like
                      ? parameter_realizations.cwiseQuotient(std_dev_pulse_)
                      : parameter_realizations.cwiseQuotient(std_dev_nopulse_);
        max_epsilon = epsilon.cwiseAbs().maxCoeff();
      }

      // Random realization of model parameters in normal space
      model_params = predicted_model_params + parameter_realizations;
      // Transform random realization to real space
      transform_parameters_from_normal_space(pulse_like, model_params);

      // Additional check on pulse-like parameters
      if (pulse_like) {
	test = model_params(4) - 0.5 * model_params(1) * model_params(2);
      } else {
	test = 1.0;
      }
    }

    // Set current row of parameter realizations to generated model parameters
    simulated_params.row(i) = model_params;
  }
  
  return simulated_params;
}

Eigen::VectorXd
    stochastic::LiningDiaozemin_MP::compute_transformed_model_parameters(
        bool pulse_like) const {
  // Calculate parameters and create parameter vector
  double depth_parameter = depth_to_rupt_ < 1.0 ? depth_to_rupt_ : 1.0;
  double site_parameter = vs30_ <= 1100.0 ? std::log(vs30_) : std::log(1100.0);
  double fault_parameter =
      faulting_ == stochastic::FaultType::StrikeSlip ? 0.0 : 1.0;

  Eigen::VectorXd params_vector(8);
  params_vector << 1.0, moment_magnitude_,
      std::pow(moment_magnitude_ - magnitude_baseline_,
               static_cast<double>(moment_magnitude_ > magnitude_baseline_)),
      std::log(std::sqrt(rupture_dist_ * rupture_dist_ + c6_ * c6_)),
      moment_magnitude_ *
          std::log(std::sqrt(rupture_dist_ * rupture_dist_ + c6_ * c6_)),
      fault_parameter * depth_parameter, site_parameter, s_or_d_;

  if (std::abs(moment_magnitude_ - magnitude_baseline_) < 1e-16) {
    params_vector(2) = 0.0;
  }

  // Calculate the mean predicted model parameters in normal space
  if (pulse_like) {
    return beta_distribution_pulse_ * params_vector;
  } else {
    return beta_distribution_nopulse_ * params_vector;
  }
}

void stochastic::LiningDiaozemin_MP::transform_parameters_from_normal_space(
    bool pulse_like, Eigen::VectorXd& parameters) {
  Eigen::VectorXd transformed_params(parameters.size());
  auto standard_normal =
      Factory<stochastic::Distribution, double, double>::instance()->create(
          "NormalDist", std::move(0.0), std::move(1.0));

  if (pulse_like) {
    std::vector<unsigned int> indices = {0, 1, 4, 5, 6, 7, 8, 9, 12, 13, 14, 15, 16};
    for (auto const& index : indices) {
      transformed_params(index) = std::exp(parameters(index));
    }

    // Calculate gamma
    auto beta_dist =
        Factory<stochastic::Distribution, double, double>::instance()->create(
            "BetaDist", std::move(params_fitted1_(2)),
            std::move(params_fitted2_(2)));

    transformed_params(2) =
        ((beta_dist->inv_cumulative_dist_func(
              standard_normal->cumulative_dist_func(
                  std::vector<double>{parameters(2)})))
             .at(0)) *
            (params_upper_bound_(2) - params_lower_bound_(2)) +
        params_lower_bound_(2);

    // Calculate nu
    auto uniform_dist =
        Factory<stochastic::Distribution, double, double>::instance()->create(
            "UniformDist", std::move(params_lower_bound_(3)),
            std::move(params_upper_bound_(3)));

    transformed_params(3) = (uniform_dist->inv_cumulative_dist_func(
                                 standard_normal->cumulative_dist_func(
                                     std::vector<double>{parameters(3)})))
                                .at(0);

    // Calculate f' residual
    transformed_params(10) =
        inv_double_exp(standard_normal->cumulative_dist_func(
                           std::vector<double>{parameters(10)})[0],
                       params_fitted1_(10), params_fitted2_(10),
                       params_fitted3_(10), params_lower_bound_(10));

    // Calculate depth_to_rupt residual
    beta_dist =
        Factory<stochastic::Distribution, double, double>::instance()->create(
            "BetaDist", std::move(params_fitted1_(11)),
            std::move(params_fitted2_(11)));

    transformed_params(11) =
        std::exp(((beta_dist->inv_cumulative_dist_func(
                       standard_normal->cumulative_dist_func(
                           std::vector<double>{parameters(11)})))
                      .at(0)) *
                     (params_upper_bound_(11) - params_lower_bound_(11)) +
                 params_lower_bound_(11));

    // Calculate f' pulse-only
    transformed_params(17) =
        inv_double_exp(standard_normal->cumulative_dist_func(
                           std::vector<double>{parameters(17)})[0],
                       params_fitted1_(17), params_fitted2_(17),
                       params_fitted3_(17), params_lower_bound_(17));

    // Calculate depth_to_rupt pulse-only
    beta_dist =
        Factory<stochastic::Distribution, double, double>::instance()->create(
            "BetaDist", std::move(params_fitted1_(18)),
            std::move(params_fitted2_(18)));

    transformed_params(18) =
        std::exp(((beta_dist->inv_cumulative_dist_func(
                       standard_normal->cumulative_dist_func(
                           std::vector<double>{parameters(18)})))
                      .at(0)) *
                     (params_upper_bound_(18) - params_lower_bound_(18)) +
                 params_lower_bound_(18));
  } else {
    std::vector<unsigned int> indices = {0, 1, 2, 3, 4, 7, 8, 9, 10, 11};
    for (auto const& index : indices) {
      transformed_params(index) = std::exp(parameters(index));
    }

    // Calculate f' component 1
    transformed_params(5) =
        inv_double_exp(standard_normal->cumulative_dist_func(
                           std::vector<double>{parameters(5)})[0],
                       params_fitted1_(10), params_fitted2_(10),
                       params_fitted3_(10), params_lower_bound_(10));

    // Calculate depth_to_rupture component 1
    auto beta_dist =
        Factory<stochastic::Distribution, double, double>::instance()->create(
            "BetaDist", std::move(params_fitted1_(11)),
            std::move(params_fitted2_(11)));

    transformed_params(6) =
        std::exp(((beta_dist->inv_cumulative_dist_func(
                       standard_normal->cumulative_dist_func(
                           std::vector<double>{parameters(6)})))
                      .at(0)) *
                     (params_upper_bound_(11) - params_lower_bound_(11)) +
                 params_lower_bound_(11));

    // Calculate f' component 2
    transformed_params(12) =
        inv_double_exp(standard_normal->cumulative_dist_func(
                           std::vector<double>{parameters(12)})[0],
                       params_fitted1_(17), params_fitted2_(17),
                       params_fitted3_(17), params_lower_bound_(17));

    // Calculate depth_to_rupture compenent 2
    beta_dist =
        Factory<stochastic::Distribution, double, double>::instance()->create(
            "BetaDist", std::move(params_fitted1_(18)),
            std::move(params_fitted2_(18)));

    transformed_params(13) =
        std::exp(((beta_dist->inv_cumulative_dist_func(
                       standard_normal->cumulative_dist_func(
                           std::vector<double>{parameters(13)})))
                      .at(0)) *
                     (params_upper_bound_(18) - params_lower_bound_(18)) +
                 params_lower_bound_(18));
  }

  // Set input vector of parameters to transformed parameters
  parameters = transformed_params;
}

double stochastic::LiningDiaozemin_MP::inv_double_exp(
    double probability, double param_a, double param_b, double param_c,
    double lower_bound) const {
  if (probability < 0.0 || probability > 1.0) {
    throw std::runtime_error(
        "\nERROR: in stochastic::LiningDiaozemin::inv_double_exp: "
        "Probability argument less than 0.0 or greater than 1.0\n");
  }

  double location_inv =
      (1.0 / param_b) * std::log((param_b / param_c) * probability +
                                 std::exp(param_b * lower_bound));

  if (location_inv < lower_bound || location_inv > 0.0) {
    location_inv =
        -(1.0 / param_a) *
        std::log((param_a / param_b) * (1.0 - std::exp(param_b * lower_bound)) -
                 (param_a / param_c) * probability + 1.0);
  }

  return location_inv;
}

void stochastic::LiningDiaozemin_MP::simulate_near_fault_ground_motion(
    bool pulse_like, const Eigen::VectorXd& parameters,
    std::vector<std::vector<double>>& accel_comp_1,
    std::vector<std::vector<double>>& accel_comp_2,
    unsigned int num_gms) const {

  // Extract parameters for two components of ground motion
  Eigen::VectorXd alpha_1(7);
  Eigen::VectorXd alpha_2(7);  
  if (pulse_like) {
    alpha_1 = parameters.segment(5, 7);
    alpha_2 = parameters.segment(12, 7);
  } else {
    alpha_1 = parameters.segment(0, 7);
    alpha_2 = parameters.segment(7, 7);
  }

  // Set modulating and filter parameters
  Eigen::VectorXd modulating_params_1 =
      backcalculate_modulating_params(alpha_1.segment(0, 4), start_time_);
  Eigen::VectorXd modulating_params_2 =
      backcalculate_modulating_params(alpha_2.segment(0, 4), start_time_);

  Eigen::VectorXd filter_params_1 = alpha_1.segment(4, 3);
  Eigen::VectorXd filter_params_2 = alpha_2.segment(4, 3);

  // Determine length of time for simulation
  double t95 = start_time_ + alpha_1[1] + alpha_1[2] >
                       start_time_ + alpha_2[1] + alpha_2[2]
                   ? start_time_ + alpha_1[1] + alpha_1[2]
                   : start_time_ + alpha_2[1] + alpha_2[2];

  unsigned int num_steps =
      static_cast<unsigned int>(std::ceil(2.5 * t95 / time_step_));

  num_steps = num_steps % 2 == 1 ? num_steps + 1 : num_steps;

  // Generated modulated filtered white noise
  auto white_noise_1 = simulate_white_noise(
      modulating_params_1, filter_params_1, num_steps, num_gms);
  auto white_noise_2 = simulate_white_noise(
      modulating_params_2, filter_params_2, num_steps, num_gms);

  // Calculate high-pass filter and padding
  double freq_corner = std::pow(10.0, 1.4071 - 0.3452 * moment_magnitude_);
  unsigned int filter_order = 4;
  double padding_duration = 0.5 * 1.5 * filter_order / freq_corner;
  unsigned int num_pads =
      static_cast<unsigned int>(std::ceil(padding_duration / time_step_));

  // Add zero-padding
  Eigen::MatrixXd accel_padded_1 =
      Eigen::MatrixXd::Zero(num_gms, num_pads + num_steps + num_pads);
  Eigen::MatrixXd accel_padded_2 =
      Eigen::MatrixXd::Zero(num_gms, num_pads + num_steps + num_pads);

  for (unsigned int i = 0; i < num_gms; ++i) {
    // Pad component 1
    accel_padded_1.block(i, 0, 1, num_pads) =
        Eigen::RowVectorXd::Zero(num_pads);
    accel_padded_1.block(i, num_pads - 1, 1, num_steps) = white_noise_1.row(i);
    accel_padded_1.block(i, num_pads + num_steps - 1, 1, num_pads) =
        Eigen::RowVectorXd::Zero(num_pads);

    // Pad component 2
    accel_padded_2.block(i, 0, 1, num_pads) =
        Eigen::RowVectorXd::Zero(num_pads);
    accel_padded_2.block(i, num_pads - 1, 1, num_steps) = white_noise_2.row(i);
    accel_padded_2.block(i, num_pads + num_steps - 1, 1, num_pads) =
        Eigen::RowVectorXd::Zero(num_pads);
  }

  // Apply filter to padded acceleration time histories
  accel_comp_1.resize(num_gms);
  accel_comp_2.resize(num_gms);
  for (unsigned int i = 0; i < num_gms; ++i) {
    accel_comp_1[i] = filter_acceleration(accel_padded_1.row(i), freq_corner, filter_order);
    accel_comp_2[i] = filter_acceleration(accel_padded_2.row(i), freq_corner, filter_order);    
  }

  // Rescale time histories for energy consistency:
  // Target Arias intensity for rescaling after high-pass filter in g-sec
  double target_ai_1 = alpha_1(0) / 981;
  double target_ai_2 = alpha_2(0) / 981;

  std::vector<std::vector<double>> arias_intensity_1(
      num_gms, std::vector<double>(accel_comp_1[0].size()));
  std::vector<std::vector<double>> arias_intensity_2(
      num_gms, std::vector<double>(accel_comp_2[0].size()));

  // Calculate Arias intensity
  for (unsigned int i = 0; i < num_gms; ++i) {
    std::transform(accel_comp_1[i].begin(), accel_comp_1[i].end(),
                   arias_intensity_1[i].begin(),
                   [this](double value) -> double {
                     return value * value * time_step_ * M_PI / 2.0;
                   });

    std::partial_sum(arias_intensity_1[i].begin(), arias_intensity_1[i].end(),
                     arias_intensity_1[i].begin());

    std::transform(accel_comp_2[i].begin(), accel_comp_2[i].end(),
                   arias_intensity_2[i].begin(),
                   [this](double value) -> double {
                     return value * value * time_step_ * M_PI / 2.0;
                   });

    std::partial_sum(arias_intensity_2[i].begin(), arias_intensity_2[i].end(),
                     arias_intensity_2[i].begin());
  }

  // Calculate scaling factors and scale accelerations to match Arias intensity
  for (unsigned int i = 0; i < num_gms; ++i) {
    double scale_factor_1 = std::sqrt(
        target_ai_1 / arias_intensity_1[i][arias_intensity_1[i].size() - 1]);
    double scale_factor_2 = std::sqrt(
        target_ai_2 / arias_intensity_2[i][arias_intensity_2[i].size() - 1]);

    std::transform(accel_comp_1[i].begin(), accel_comp_1[i].end(),
                   accel_comp_1[i].begin(),
                   [&scale_factor_1](double value) -> double {
                     return value * scale_factor_1;
                   });

    std::transform(accel_comp_2[i].begin(), accel_comp_2[i].end(),
                   accel_comp_2[i].begin(),
                   [&scale_factor_2](double value) -> double {
                     return value * scale_factor_2;
                   });
  }

  // If pulse-like, add pulse acceleration to component 1 direction
  if (pulse_like) {
    // Calculate pulse acceleration
    auto pulse_accel = calc_pulse_acceleration(num_steps, parameters);

    // Add pulse motion to component 1
    for (unsigned int i = 0; i < num_gms; ++i) {
      for (unsigned int j = 0; j < pulse_accel.size(); ++j) {
        accel_comp_1[i][j + num_pads - 1] =
            accel_comp_1[i][j + num_pads - 1] + pulse_accel[j];
      }
    }
  }
}

Eigen::VectorXd stochastic::LiningDiaozemin_MP::backcalculate_modulating_params(
        const Eigen::VectorXd& q_params, double t0) const {
  double arias_intensity = q_params(0) / 981,  // Convert from cm/s to g-s
    d595 = q_params(1), d05 = q_params(2),
    d030 = q_params(3), d095 = d05 + d595,
    t30 = t0 + d030;

  // Search for local minimum by trying several starting points
  optimization::NelderMead minimizer(1e-10);
  std::vector<double> diffs(6);
  std::function<double(const std::vector<double>&)> error_function =
      std::bind(&stochastic::LiningDiaozemin_MP::calc_parameter_error, this,
                std::placeholders::_1, d05, d030, d095, t0);

  // Iterate over starting points
  std::vector<std::vector<double>> starting_points = {
      {1.0, 0.2, t30}, {2.0, 0.2, t30}, {5.0, 0.2, t30},
      {1.0, 1.0, t30}, {2.0, 1.0, t30}, {5.0, 1.0, t30}};
  std::vector<double> deltas(starting_points[0].size());

  unsigned int point_counter = 0;
  for (auto& point : starting_points) {
    for (unsigned int i = 0; i < deltas.size(); ++i) {
      deltas[i] = std::abs(point[i]) < 1.0e-6 ? 0.00025 : 0.05 * std::abs(point[i]);
    }

    point = minimizer.minimize(point, deltas, error_function);
    diffs[point_counter] = error_function(point);
    ++point_counter;
  }

  // To avoid negative values of alpha, multiply cost value by 10000 if so
  for (unsigned int i = 0; i < starting_points.size(); ++i) {
    if (starting_points[i][0] < 0.0) {
      diffs[i] *= 10000.0;
    }
  }

  // Find solution that matches targe values best
  auto min_index = std::distance(
      std::begin(diffs), std::min_element(std::begin(diffs), std::end(diffs)));

  Eigen::VectorXd parameters(4);
  parameters << starting_points[min_index][0], starting_points[min_index][1],
      starting_points[min_index][2], std::numeric_limits<double>::infinity();

  parameters[3] = std::sqrt(
      arias_intensity /
      ((M_PI / 2.0) * ((parameters[2] - t0) / (2.0 * parameters[0] + 1.0) +
                       1.0 / (2.0 * parameters[1]))));

  return parameters;
}

double stochastic::LiningDiaozemin_MP::calc_parameter_error(
    const std::vector<double>& parameters, double d05_target,
    double d030_target, double d095_target, double t0) const {
  // Modulating function parameters
  double alpha = parameters[0], beta = parameters[1], t_max_q = parameters[2];

  // Arias intensity times corresponding to the selected modulating function
  // and parameters
  double t5_fit =
      t0 + std::pow((5.0 / 100.0) * std::pow(t_max_q - t0, 2.0 * alpha) *
                        ((t_max_q - t0) + (2.0 * alpha + 1.0) / (2.0 * beta)),
                    1.0 / (2.0 * alpha + 1.0));

  double t30_fit =
      t0 + std::pow((30.0 / 100.0) * std::pow(t_max_q - t0, 2.0 * alpha) *
                        ((t_max_q - t0) + (2.0 * alpha + 1.0) / (2.0 * beta)),
                    1.0 / (2.0 * alpha + 1.0));

  double t95_fit =
      t0 + std::pow((95.0 / 100.0) * std::pow(t_max_q - t0, 2.0 * alpha) *
                        ((t_max_q - t0) + (2.0 * alpha + 1.0) / (2.0 * beta)),
                    1.0 / (2.0 * alpha + 1.0));

  if (t5_fit > t_max_q) {
    t5_fit = t_max_q -
             (1.0 / (2.0 * beta)) *
                 std::log(((100.0 - 5.0) / 100.0) *
                          ((t_max_q - t0) * (2.0 * beta) / (2.0 * alpha + 1.0) +
                           1.0));
  }

  if (t30_fit > t_max_q) {
    t30_fit =
        t_max_q -
        (1.0 / (2.0 * beta)) *
            std::log(
                ((100.0 - 30.0) / 100.0) *
                ((t_max_q - t0) * (2.0 * beta) / (2.0 * alpha + 1.0) + 1.0));
  }

  if (t95_fit > t_max_q) {
    t95_fit =
        t_max_q -
        (1.0 / (2.0 * beta)) *
            std::log(
                ((100.0 - 95.0) / 100.0) *
                ((t_max_q - t0) * (2.0 * beta) / (2.0 * alpha + 1.0) + 1.0));
  }

  // Duration parameters of corresponding modulating function
  double d05_fit = t5_fit - t0;
  double d030_fit = t30_fit - t0;
  double d095_fit = t95_fit - t0;

  // Error measure
  // NOTE: It doesn't matter whether the terms are normalized or not
  return std::pow(d05_target - d05_fit, 2) +
         std::pow(d030_target - d030_fit, 2) +
         std::pow(d095_target - d095_fit, 2);
}

Eigen::MatrixXd stochastic::LiningDiaozemin_MP::simulate_white_noise(
    const Eigen::VectorXd& modulating_params,
    const Eigen::VectorXd& filter_params, unsigned int num_steps,
    unsigned int num_gms) const {
  // CALCULATE MODULATING FUNCTION:
  auto modulating_func =
      calc_modulating_func(num_steps, start_time_, modulating_params);

  // CALCULATE FREQUENCY FUNCTION:
  // For any general modulating function, get the discretized times of interest
  // Lower bound before t01
  double t01 = calc_time_to_intensity(modulating_func, 1.0);
  // Middle set to t30
  double tmid = calc_time_to_intensity(modulating_func, 30.0);
  // Upper bound after t99
  double t99 = calc_time_to_intensity(modulating_func, 99.0);

  // Define the filter frequency and bandwidth
  auto frequency_filter =
      calc_linear_filter(num_steps, filter_params, t01, tmid, t99);

  // Generate white noise
  auto generator =
      seed_value_ != std::numeric_limits<int>::infinity()
          ? boost::random::mt19937(static_cast<unsigned int>(seed_value_))
          : boost::random::mt19937(
                static_cast<unsigned int>(std::time(nullptr)));
  
  boost::random::normal_distribution<> distribution(0.0, 1.0);
  boost::random::variate_generator<boost::random::mt19937&,
                                   boost::random::normal_distribution<>>
      noise_gen(generator, distribution);

  Eigen::MatrixXd white_noise(num_gms, num_steps);
  for (unsigned int i = 0; i < num_gms; ++i) {
    for (unsigned int j = 0; j < num_steps; ++j) {
      white_noise(i, j) = noise_gen();
    }
  }

  // Calculate impulse response
  Eigen::MatrixXd impulse_response = calc_impulse_response_filter(
      num_steps, frequency_filter, filter_params(2));

  auto freq_func = white_noise * impulse_response;

  Eigen::MatrixXd filtered_white_noise(num_gms, num_steps);
  // Convert modulating function to Eigen::VectorXd
  Eigen::VectorXd mod_func_vec = Eigen::Map<Eigen::VectorXd>(
      modulating_func.data(), modulating_func.size());

  for (unsigned int i = 0; i < num_gms; ++i) {
    filtered_white_noise.row(i) = freq_func.row(i).cwiseProduct(mod_func_vec.transpose());
  }

  return filtered_white_noise;
}

std::vector<double> stochastic::LiningDiaozemin_MP::calc_modulating_func(
    unsigned int num_steps, double t0,
    const Eigen::VectorXd& parameters) const {

  std::vector<double> mod_func_vals(num_steps);

  for (unsigned int i = 0; i < num_steps; ++i) {
    double time = static_cast<double>(i) * time_step_;

    if (time < t0) {
      mod_func_vals[i] = 0.0;
    } else if (time < parameters(2)) {
      mod_func_vals[i] =
          parameters(3) *
          std::pow((time - t0) / (parameters(2) - t0), parameters[0]);
    } else {
      mod_func_vals[i] =
          parameters(3) * std::exp(-parameters(1) * (time - parameters[2]));
    }
  }

  return mod_func_vals;
}

double stochastic::LiningDiaozemin_MP::calc_time_to_intensity(
    const std::vector<double>& acceleration, double percentage) const {
  // Calculate cumulative energy in acceleration time series, which is
  // proportional to Arias intensity
  std::vector<double> t01_sum(acceleration.size());

  std::transform(acceleration.begin(), acceleration.end(), t01_sum.begin(),
                [](double value) -> double { return value * value; });

  std::partial_sum(t01_sum.begin(), t01_sum.end(), t01_sum.begin());

  // Calculate normalized cumulative Arias intensity in percent
  std::transform(t01_sum.begin(), t01_sum.end(), t01_sum.begin(),
                 [&t01_sum](double value) -> double {
                   return value / t01_sum[t01_sum.size() - 1] * 100.0;
                 });

  return time_step_ *
         static_cast<double>(
             std::distance(t01_sum.begin(),
                           std::find_if(t01_sum.begin(), t01_sum.end(),
                                        [percentage](double value) {
                                          return value >= percentage;
                                        })) +
             1);
}

std::vector<double> stochastic::LiningDiaozemin_MP::calc_linear_filter(
    unsigned int num_steps, const Eigen::VectorXd& filter_params, double t01,
    double tmid, double t99) const {
  // Mininum frequency in Hz
  double min_freq = 0.3;
  std::vector<double> filter_func(num_steps);
  // Frequency at tmid, in Hz  
  double mid_freq = filter_params(0);
  // Slope of frequency assumed constant
  double freq_slope = filter_params(1);

  for (unsigned int i = 0; i < num_steps; ++i) {
    double current_time = i * time_step_;
    if (current_time < t01) {
      filter_func[i] =
          min_freq > mid_freq + freq_slope * (t01 - tmid)
              ? min_freq * 2.0 * M_PI
              : (mid_freq + freq_slope * (t01 - tmid)) * 2.0 * M_PI;
    } else if (current_time <= t99) {
      filter_func[i] =
          min_freq > mid_freq + freq_slope * (current_time - tmid)
              ? min_freq * 2.0 * M_PI
              : (mid_freq + freq_slope * (current_time - tmid)) * 2.0 * M_PI;
    } else {
      filter_func[i] =
          min_freq > mid_freq + freq_slope * (t99 - tmid)
              ? min_freq * 2.0 * M_PI
              : (mid_freq + freq_slope * (t99 - tmid)) * 2.0 * M_PI;
    }
  }

  return filter_func;
}

Eigen::MatrixXd stochastic::LiningDiaozemin_MP::calc_impulse_response_filter(
    unsigned int num_steps, const std::vector<double>& input_filter,
    double zeta) const {
  Eigen::MatrixXd impulse_response =
      Eigen::MatrixXd::Zero(num_steps, num_steps);

  for (unsigned int i = 0; i < num_steps; ++i) {
    double omega = input_filter[i];
    Eigen::VectorXd times(num_steps - i);
    
    for (unsigned int j = 0; j < times.size(); ++j) {
      times(j) = static_cast<double>(j) * time_step_;
    }

    impulse_response.block(i, i, 1, times.size()) =
        ((omega / std::sqrt(1.0 - zeta * zeta)) *
         ((-zeta * omega * times).array().exp()) *
         ((omega * std::sqrt(1.0 - zeta * zeta) * times).array().sin()))
            .matrix()
            .transpose();
  }

  Eigen::VectorXd denominator =
      ((impulse_response.array().pow(2.0)).matrix().colwise().sum())
          .array()
          .sqrt();
  denominator(0) = 0.1;
  
  for (unsigned int i = 0; i < impulse_response.rows(); ++i) {
    impulse_response.row(i) =
        impulse_response.row(i).cwiseQuotient(denominator.transpose());
  }

  return impulse_response;
}

std::vector<double> stochastic::LiningDiaozemin_MP::filter_acceleration(
    const Eigen::VectorXd& accel_history, double freq_corner,
    unsigned int filter_order) const {

  // Calculate normalized cutoff frequency
  std::vector<std::complex<double>> accel_fft(accel_history.size());

  // Compute FFT of acceleration history
  numeric_utils::fft(accel_history, accel_fft);

  // Get filter coefficients
  auto filter = Dispatcher<std::vector<double>, double, double, unsigned int,
                           unsigned int>::instance()
                    ->dispatch("AcausalHighpassButterworth", freq_corner,
                               time_step_, filter_order, accel_fft.size());

  // Filter acceleration in frequency domain
  for (unsigned int i = 0; i < accel_fft.size(); ++i) {
    accel_fft[i] = accel_fft[i] * filter[i];
  }

  // Compute inverse FFT of filtered transformed acceleration
  std::vector<double> filtered_acc(accel_fft.size());
  numeric_utils::inverse_fft(accel_fft, filtered_acc);

  return filtered_acc;
}

std::vector<double> stochastic::LiningDiaozemin_MP::calc_pulse_acceleration(
    unsigned int num_steps, const Eigen::VectorXd& parameters) const {
  double pulse_velocity = parameters(0);  
  double pulse_frequency = 1.0 / parameters(1);
  double oscillation_param = parameters(2);  
  double phase_angle = parameters(3) * M_PI;
  double peak_time = start_time_ + parameters(4);

  double resp_disp = pulse_velocity / (4.0 * M_PI * pulse_frequency) *
                         std::sin(phase_angle + oscillation_param * M_PI) /
                         (1 - oscillation_param * oscillation_param) -
                     pulse_velocity / (4.0 * M_PI * pulse_frequency) *
                         std::sin(phase_angle - oscillation_param * M_PI) /
                         (1.0 - oscillation_param * oscillation_param);

  // Calculate pulse velocity time history
  std::vector<double> velocity_history(num_steps);

  for (unsigned int i = 0; i < num_steps; ++i) {
    double time = static_cast<double>(i) * time_step_;
    if (time > (peak_time - 0.5 * oscillation_param / pulse_frequency) &&
        time <= (peak_time + 0.5 * oscillation_param / pulse_frequency)) {
      velocity_history[i] =
          (0.5 * pulse_velocity *
               std::cos(2.0 * M_PI * pulse_frequency * (time - peak_time) +
                        phase_angle) -
           resp_disp * pulse_frequency / oscillation_param) *
          (1.0 + std::cos(2.0 * M_PI * pulse_frequency * (time - peak_time) /
                          oscillation_param));
    }
    else {
      velocity_history[i] = 0.0;
    }
  }
  
  // Calculate pulse acceleration time history
  return numeric_utils::derivative(velocity_history, 1.0 / (981.0 * time_step_), true);
}

void stochastic::LiningDiaozemin_MP::truncate_time_histories(
    std::vector<std::vector<double>>& accel_comp_1,
    std::vector<std::vector<double>>& accel_comp_2, double gfactor,
    double amplitude_lim, double pgd_lim) const {

  // Iterate over time histories
  for (unsigned int i = 0; i < accel_comp_1.size(); ++i) {
    // Calculate peak ground displacement (PGD):
    // Component 1
    std::vector<double> vel_comp_1(accel_comp_1[i].size());
    std::vector<double> disp_comp_1(accel_comp_1[i].size());

    std::transform(accel_comp_1[i].begin(), accel_comp_1[i].end(),
                   vel_comp_1.begin(),
                   [&gfactor, this](double value) -> double {
                     return value * gfactor * time_step_;
                   });

    std::partial_sum(vel_comp_1.begin(), vel_comp_1.end(),
                     vel_comp_1.begin());

    std::transform(
        vel_comp_1.begin(), vel_comp_1.end(), disp_comp_1.begin(),
        [this](double value) -> double { return value * time_step_; });

    std::partial_sum(disp_comp_1.begin(), disp_comp_1.end(),
                     disp_comp_1.begin());

    double pgd_1 = *std::max_element(disp_comp_1.begin(), disp_comp_1.end());

    double disp_limit_1 =
        amplitude_lim < pgd_1 * pgd_lim ? amplitude_lim : pgd_1 * pgd_lim;

    // Component 2
    std::vector<double> vel_comp_2(accel_comp_2[i].size());
    std::vector<double> disp_comp_2(accel_comp_2[i].size());

    std::transform(accel_comp_2[i].begin(), accel_comp_2[i].end(),
                   vel_comp_2.begin(),
                   [&gfactor, this](double value) -> double {
                     return value * gfactor * time_step_;
                   });

    std::partial_sum(vel_comp_2.begin(), vel_comp_2.end(),
                     vel_comp_2.begin());

    std::transform(
        vel_comp_2.begin(), vel_comp_2.end(), disp_comp_2.begin(),
        [this](double value) -> double { return value * time_step_; });

    std::partial_sum(disp_comp_2.begin(), disp_comp_2.end(),
                     disp_comp_2.begin());

    double pgd_2 = *std::max_element(disp_comp_2.begin(), disp_comp_2.end());

    double disp_limit_2 =
        amplitude_lim < pgd_2 * pgd_lim ? amplitude_lim : pgd_2 * pgd_lim;

    // Calculate displacement limit indices:
    // Component 1
    unsigned int initial_index_1 =
        static_cast<unsigned int>(
            std::distance(disp_comp_1.begin(),
                          std::find_if(disp_comp_1.begin(), disp_comp_1.end(),
                                       [&disp_limit_1](double value) {
                                         return value > disp_limit_1;
                                       }))) -
        1;

    unsigned int final_index_1 =
        static_cast<unsigned int>(disp_comp_1.size()) -
        static_cast<unsigned int>(
            std::distance(disp_comp_1.rbegin(),
                          std::find_if(disp_comp_1.rbegin(), disp_comp_1.rend(),
                                       [&disp_limit_1](double value) {
                                         return value > disp_limit_1;
                                       })));

    // Component 2
    unsigned int initial_index_2 =
        static_cast<unsigned int>(
            std::distance(disp_comp_2.begin(),
                          std::find_if(disp_comp_2.begin(), disp_comp_2.end(),
                                       [&disp_limit_2](double value) {
                                         return value > disp_limit_2;
                                       }))) -
        1;

    unsigned int final_index_2 =
        static_cast<unsigned int>(disp_comp_2.size()) -
        static_cast<unsigned int>(
            std::distance(disp_comp_2.rbegin(),
                          std::find_if(disp_comp_2.rbegin(), disp_comp_2.rend(),
                                       [&disp_limit_2](double value) {
                                         return value > disp_limit_2;
                                       })));

    // Truncate acceleration
    unsigned int initial_index =
        initial_index_1 <= initial_index_2 ? initial_index_1 : initial_index_2;
    unsigned int final_index =
        final_index_1 >= final_index_2 ? final_index_1 : final_index_2;

    if (final_index == disp_comp_1.size() - 1) {
      final_index -= 1;
    }

    accel_comp_1[i] =
        std::vector<double>(accel_comp_1[i].begin() + initial_index,
                            accel_comp_1[i].begin() + final_index);
    accel_comp_2[i] =
        std::vector<double>(accel_comp_2[i].begin() + initial_index,
                            accel_comp_2[i].begin() + final_index);
    
  }
}

void stochastic::LiningDiaozemin_MP::baseline_correct_time_history(
    std::vector<double>& time_history, double gfactor,
    unsigned int order) const {

  // Calculate velocity and displacment time histories
  std::vector<double> vel_series(time_history.size());
  std::vector<double> disp_series(time_history.size());

  std::transform(time_history.begin(), time_history.end(), vel_series.begin(),
                 [&gfactor, this](double value) -> double {
                   return value * gfactor * time_step_;
                 });

  std::partial_sum(vel_series.begin(), vel_series.end(), vel_series.begin());

  std::transform(vel_series.begin(), vel_series.end(), disp_series.begin(),
                 [this](double value) -> double { return value * time_step_; });

  std::partial_sum(disp_series.begin(), disp_series.end(), disp_series.begin());

  Eigen::VectorXd times(time_history.size());

  for (unsigned int i = 0; i < times.size(); ++i) {
    times(i) = i * time_step_;
  }

  // Convert input time history from std vector to Eigen vector
  Eigen::VectorXd disp_vector =
      Eigen::Map<Eigen::VectorXd>(disp_series.data(), disp_series.size());

  // Fit zero-intercept polynomial to displacement time history
  auto displacement_poly =
    numeric_utils::polyfit_intercept(times, disp_vector, 0.0, 5);
  auto velocity_poly = numeric_utils::polynomial_derivative(displacement_poly);
  auto accel_poly = numeric_utils::polynomial_derivative(velocity_poly);

  // Calculate acceleration correction based on polynomial
  auto accel_correction =
      numeric_utils::evaluate_polynomial(accel_poly, times) / gfactor;

  // Correct time series based on acceleration correction
  for (unsigned int i = 0; i < accel_correction.size(); ++i) {
    time_history[i] = time_history[i] - accel_correction(i);
  }
}

void stochastic::LiningDiaozemin_MP::convert_time_history_units(
    std::vector<double>& time_history, bool units) const {
  double conversion_factor = units ? 1.0 : 9.81;
 
  for (auto& val : time_history) {
    val = val * conversion_factor;
  }
}
