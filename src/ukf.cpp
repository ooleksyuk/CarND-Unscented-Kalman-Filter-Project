#include "ukf.h"
#include "Eigen/Dense"
#include <iostream>

using namespace std;
using Eigen::MatrixXd;
using Eigen::VectorXd;
using std::vector;

#define EPS 0.001

/**
 * Initializes Unscented Kalman filter
 */
UKF::UKF() {
  // if this is false, laser measurements will be ignored (except during init)
  use_laser_ = true;

  // if this is false, radar measurements will be ignored (except during init)
  use_radar_ = true;

  // initial state vector
  x_ = VectorXd(5);
  x_.fill(0.0);

  // initial covariance matrix
  P_ = MatrixXd(5, 5);
  P_.setIdentity(5, 5);

  // Process noise standard deviation longitudinal acceleration in m/s^2
  std_a_ = 3.0;

  // Process noise standard deviation yaw acceleration in rad/s^2
  std_yawdd_ = M_PI / 8.0;

  // Laser measurement noise standard deviation position1 in m
  std_laspx_ = 0.15;

  // Laser measurement noise standard deviation position2 in m
  std_laspy_ = 0.15;

  // Radar measurement noise standard deviation radius in m
  std_radr_ = 0.3;

  // Radar measurement noise standard deviation angle in rad
  std_radphi_ = 0.03;

  // Radar measurement noise standard deviation radius change in m/s
  std_radrd_ = 0.3;

  /**
  TODO:

  Complete the initialization. See ukf.h for other member properties.

  Hint: one or more values initialized above might be wildly off...
  */
  // initially set to false, it will be set to true in first call of ProcessMeasurement
  is_initialized_ = false;

  // time when the state is true, in us
  time_us_ = 0;

  // state dimension
  n_x_ = x_.size();

  // Augmented state dimension
  n_aug_ = n_x_ + 2;

  // Sigma point spreading parameter
  lambda_ = 3 - n_x_;

  n_sig_ = 2 * n_aug_ + 1;

  // predicted sigma points matrix
  Xsig_pred_ = MatrixXd(n_x_, n_sig_);
  Xsig_pred_.fill(0.0);

  //create vector for weights
  weights_ = VectorXd(n_sig_);

  // set weights of sigma points
  weights_(0) = lambda_ / (lambda_ + n_aug_);
  for (int i = 1; i < n_aug_; i++) {
    weights_(i) = 0.5 / (n_aug_ + lambda_);
  }

  // the current NIS for radar
  NIS_radar_ = 0.0;

  // the current NIS for laser
  NIS_laser_ = 0.0;

  // Measurement noise covariance matrices initialization
  R_radar_ = MatrixXd(3, 3);
  R_radar_ << std_radr_ * std_radr_, 0, 0,
              0, std_radphi_ * std_radphi_, 0,
              0, 0, std_radrd_ * std_radrd_;
  R_lidar_ = MatrixXd(2, 2);
  R_lidar_ << std_laspx_ * std_laspx_, 0,
              0, std_laspy_ * std_laspy_;
}

UKF::~UKF() {}

/**
 * @param {MeasurementPackage} meas_package The latest measurement data of
 * either radar or laser.
 */
void UKF::ProcessMeasurement(MeasurementPackage meas_package) {
  /**
  TODO:

  Complete this function! Make sure you switch between lidar and radar
  measurements.
  */
  /*****************************************************************************
    *  Initialize Sate
    ****************************************************************************/
  if (!is_initialized_)
  {
    // init timestamp
    time_us_ = meas_package.timestamp_;

    if (meas_package.sensor_type_ == MeasurementPackage::LASER && use_laser_)
    {
      // Deal with the special case initialisation problems
      if (fabs(meas_package.raw_measurements_[0]) < EPS and fabs(meas_package.raw_measurements_[0]) < EPS){
        x_(0) = EPS;
        x_(1) = EPS;
      }
      else {
        x_(0) = meas_package.raw_measurements_[0];
        x_(1) = meas_package.raw_measurements_[1];
      }
    }
    else if (meas_package.sensor_type_ == MeasurementPackage::RADAR && use_radar_)
    {
      /**
      Convert radar from polar to cartesian coordinates and initialize state.
      */
      // Convert radar from polar to cartesian coordinates and initialize state.
      float rho = meas_package.raw_measurements_[0]; // range
      float phi = meas_package.raw_measurements_[1]; // bearing
      float rho_dot = meas_package.raw_measurements_[2]; // velocity of rho
      // Coordinates conversion from polar to cartesian
      float px = rho * cos(phi);
      float py = rho * sin(phi);
      float vx = rho_dot * cos(phi);
      float vy = rho_dot * sin(phi);
      float v  = sqrt(vx * vx + vy * vy);
      x_ << px, py, v, 0, 0;
    }

    // done initializing, no need to predict or update
    is_initialized_ = true;

    return;
  }

  /*****************************************************************************
    *  Predict
    ****************************************************************************/
  //compute the time elapsed between the current and previous measurements
  double dt = (meas_package.timestamp_ - time_us_) / 1000000.0;	//dt - expressed in seconds
  time_us_ = meas_package.timestamp_;

  Prediction(dt);

  /*****************************************************************************
  *  Update
  ****************************************************************************/

  if (meas_package.sensor_type_ == MeasurementPackage::LASER && use_laser_)
  {
    UpdateLidar(meas_package);
  }
  else if (meas_package.sensor_type_ == MeasurementPackage::RADAR && use_radar_)
  {
    UpdateRadar(meas_package);
  }
}

/**
 * Predicts sigma points, the state, and the state covariance matrix.
 * @param {double} delta_t the change in time (in seconds) between the last
 * measurement and this one.
 */
void UKF::Prediction(double delta_t) {
  /**
  TODO:

  Complete this function! Estimate the object's location. Modify the state
  vector, x_. Predict sigma points, the state, and the state covariance matrix.
  */
  /*****************************************************************************
  *  Generate Sigma Points
  ****************************************************************************/
  //create sigma point matrix
  MatrixXd Xsig = MatrixXd(n_x_, 2 * n_x_ + 1);

  //calculate square root of P
  MatrixXd A = P_.llt().matrixL();

  //set first column of sigma point matrix
  Xsig.col(0) = x_;

  //set remaining sigma points
  for (int i = 0; i < n_x_; i++)
  {
    Xsig.col(i + 1) = x_ + sqrt(lambda_ + n_x_) * A.col(i);
    Xsig.col(i + 1 + n_x_) = x_ - sqrt(lambda_ + n_x_) * A.col(i);
  }
  /*****************************************************************************
  *  Augment Sigma Points
  ****************************************************************************/
  //create augmented mean vector
  VectorXd x_aug = VectorXd(n_aug_);

  //create augmented state covariance
  MatrixXd P_aug = MatrixXd(n_aug_, n_aug_);

  //create sigma point matrix
  MatrixXd Xsig_aug = MatrixXd(n_aug_, n_sig_);

  //create augmented mean state
  x_aug.fill(0.0);
  x_aug.head(n_x_) = x_.head(n_x_);

  //create augmented covariance matrix
  P_aug.fill(0.0);
  P_aug.topLeftCorner(n_x_, n_x_) = P_;
  P_aug(n_x_) = std_a_ * std_a_;
  P_aug(n_x_ + 1) = std_yawdd_ * std_yawdd_;

  //create square root matrix
  MatrixXd L = P_aug.llt().matrixL();

  //create augmented sigma points
  Xsig_aug.col(0) = x_aug;
  for (int i = 0; i < n_aug_; i++)
  {
    Xsig_aug.col(i + 1) = x_aug + sqrt(lambda_ + n_aug_) * L.col(i);
    Xsig_aug.col(i + 1 + n_aug_) = x_aug - sqrt(lambda_ + n_aug_) * L.col(i);
  }

  /*****************************************************************************
  *  Predict Sigma Points
  ****************************************************************************/
  //predict sigma points
  for (int i = 0; i < n_sig_; i++)
  {
    //extract values for better readability
    double p_x      = Xsig_aug(0, i);
    double p_y      = Xsig_aug(1, i);
    double v        = Xsig_aug(2, i);
    double yaw      = Xsig_aug(3, i);
    double yawd     = Xsig_aug(4, i);
    double nu_a     = Xsig_aug(5, i);
    double nu_yawdd = Xsig_aug(6, i);

    //predicted state values
    double px_p, py_p;

    if (fabs(p_x) < EPS || fabs(p_y)< EPS) {
      p_x = EPS;
      p_y = EPS;
    }

    //avoid division by zero
    if (fabs(yawd) > EPS) {
      px_p = p_x + v / yawd * (sin(yaw + yawd * delta_t) - sin(yaw));
      py_p = p_y + v / yawd * (cos(yaw) - cos(yaw + yawd * delta_t));
    }
    else {
      px_p = p_x + v * delta_t * cos(yaw);
      py_p = p_y + v * delta_t * sin(yaw);
    }

    double v_p = v;
    double yaw_p = yaw + yawd * delta_t;
    double yawd_p = yawd;

    //add noise
    px_p = px_p + 0.5 * nu_a * delta_t * delta_t * cos(yaw);
    py_p = py_p + 0.5 * nu_a * delta_t * delta_t * sin(yaw);
    v_p = v_p + nu_a * delta_t;

    yaw_p = yaw_p + 0.5 * nu_yawdd * delta_t * delta_t;
    yawd_p = yawd_p + nu_yawdd * delta_t;

    //write predicted sigma point into right column
    Xsig_pred_(0, i) = px_p;
    Xsig_pred_(1, i) = py_p;
    Xsig_pred_(2, i) = v_p;
    Xsig_pred_(3, i) = yaw_p;
    Xsig_pred_(4, i) = yawd_p;
  }
  /*****************************************************************************
  *  Convert Predicted Sigma Points to Mean/Covariance
  ****************************************************************************/
  //predicted state mean
  x_ = Xsig_pred_ * weights_;

  //predicted state covariance matrix
  P_.fill(0.0);             //******* necessary? *********
  for (int i = 0; i < n_sig_; i++) {  //iterate over sigma points

    // state difference
    VectorXd x_diff = Xsig_pred_.col(i) - x_;
    //NormAng(&(x_diff(3)));
    while (x_diff(3) > M_PI) x_diff(3) -= 2.*M_PI;
    while (x_diff(3) < -M_PI) x_diff(3) += 2.*M_PI;
    P_ = P_ + weights_(i) * x_diff * x_diff.transpose();
  }
}

/**
 * Updates the state and the state covariance matrix using a laser measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateLidar(MeasurementPackage meas_package) {
  /**
  TODO:

  Complete this function! Use lidar data to update the belief about the object's
  position. Modify the state vector, x_, and covariance, P_.

  You'll also need to calculate the lidar NIS.
  */
  //extract measurement as VectorXd
  VectorXd z = meas_package.raw_measurements_;

  //set measurement dimension, lidar can measure p_x and p_y
  int n_z = 2;

  //create matrix for sigma points in measurement space
  MatrixXd Zsig = Xsig_pred_.block(0, 0, n_z, n_sig_);

  UpdateUKF(meas_package, Zsig, n_z);
}

/**
 * Updates the state and the state covariance matrix using a radar measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateRadar(MeasurementPackage meas_package) {
  /**
  TODO:

  Complete this function! Use radar data to update the belief about the object's
  position. Modify the state vector, x_, and covariance, P_.

  You'll also need to calculate the radar NIS.
  */
  //extract measurement as VectorXd
  VectorXd z = meas_package.raw_measurements_;

  //set measurement dimension, radar can measure r, phi, and r_dot
  int n_z = 3;

  //create matrix for sigma points in measurement space
  MatrixXd Zsig = MatrixXd(n_z, n_sig_);

  //transform sigma points into measurement space
  for (int i = 0; i < n_sig_; i++) {  //2n+1 simga points

    // extract values for better readibility
    double p_x = Xsig_pred_(0, i);
    double p_y = Xsig_pred_(1, i);
    double v   = Xsig_pred_(2, i);
    double yaw = Xsig_pred_(3, i);

    double v1 = cos(yaw) * v;
    double v2 = sin(yaw) * v;

    // measurement model
    if (fabs(p_x) > EPS || fabs(p_y) > EPS) {
      Zsig(0,i) = sqrt(p_x * p_x + p_y * p_y);                          //r
      Zsig(1,i) = atan2(p_y, p_x);                                      //phi
      Zsig(2,i) = (p_x * v1 + p_y * v2 ) / sqrt(p_x * p_x + p_y * p_y); // r_dot
    }
    else {
      Zsig(0,i) = 0.0;                                 //r
      Zsig(1,i) = 0.0;                                 //phi
      Zsig(2,i) = 0.0;                                 //r_dot
    }
  }

  UpdateUKF(meas_package, Zsig, n_z);
}

void UKF::UpdateUKF(MeasurementPackage meas_package, MatrixXd Zsig, int n_z) {
  // Mean predicted measurement
  VectorXd z_pred = VectorXd(n_z);
  z_pred = Zsig * weights_;

  //measurement covariance matrix S
  MatrixXd S = MatrixXd(n_z, n_z);
  S.fill(0.0);
  for (int i = 0; i < n_sig_; i++) {
    // Residual
    VectorXd z_diff = Zsig.col(i) - z_pred;
    // Angle normalization
    NormAng(&(z_diff(1)));
    S = S + weights_(i) * z_diff * z_diff.transpose();
  }

  // Add measurement noise covariance matrix
  MatrixXd R = MatrixXd(n_z, n_z);
  R.fill(0.0);
  if (meas_package.sensor_type_ == MeasurementPackage::RADAR && use_radar_) { // Radar
    R = R_radar_;
  } else if (meas_package.sensor_type_ == MeasurementPackage::LASER && use_laser_) { // Lidar
    R = R_lidar_;
  }
  S = S + R;

  // Create matrix for cross correlation Tc
  MatrixXd Tc = MatrixXd(n_x_, n_z);
  // Calculate cross correlation matrix
  Tc.fill(0.0);
  for (int i = 0; i < n_sig_; i++) {
    //residual
    VectorXd z_diff = Zsig.col(i) - z_pred;
    if (meas_package.sensor_type_ == MeasurementPackage::RADAR && use_radar_) { // Radar
      // Angle normalization
      NormAng(&(z_diff(1)));
    }
    // State difference
    VectorXd x_diff = Xsig_pred_.col(i) - x_;
    // Angle normalization
    NormAng(&(x_diff(3)));
    Tc = Tc + weights_(i) * x_diff * z_diff.transpose();
  }

  // Measurements
  VectorXd z = meas_package.raw_measurements_;

  //Kalman gain K;
  MatrixXd K = Tc * S.inverse();

  // Residual
  VectorXd z_diff = z - z_pred;
  if (meas_package.sensor_type_ == MeasurementPackage::RADAR && use_radar_) { // Radar
    // Angle normalization
    NormAng(&(z_diff(1)));
  }

  // Update state mean and covariance matrix
  x_ = x_ + K * z_diff;
  P_ = P_ - K * S * K.transpose();

  // Calculate NIS
  if (meas_package.sensor_type_ == MeasurementPackage::RADAR && use_radar_) { // Radar
    NIS_radar_ = z.transpose() * S.inverse() * z;
  } else if (meas_package.sensor_type_ == MeasurementPackage::LASER && use_laser_) { // Lidar
    NIS_laser_ = z.transpose() * S.inverse() * z;
  }
}

void UKF::NormAng(double *ang) {
  while (*ang > M_PI) *ang -= 2. * M_PI;
  while (*ang < -M_PI) *ang += 2. * M_PI;
}