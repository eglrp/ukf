#include <ros/ros.h>
#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <sensor_msgs/Imu.h>
#include <geometry_msgs/TwistWithCovarianceStamped.h>
#include <eigen3/Eigen/Dense>
#include <tf/transform_datatypes.h>
#include <nav_msgs/Odometry.h>
geometry_msgs::PoseWithCovarianceStamped svo_pose;
sensor_msgs::Imu imu_data;
nav_msgs::Odometry filterd;
void svo_cb(const geometry_msgs::PoseWithCovarianceStamped::ConstPtr &msg){
  svo_pose = *msg;
}

void imu_cb(const sensor_msgs::Imu::ConstPtr &msg){
  imu_data = *msg;
}

struct Measurement
{
  // The measurement and its associated covariance
  std::vector<float> measurement_;
  Eigen::MatrixXd covariance_;
  int updatesize;
  double mahalanobisThresh_;


};
Measurement measurement;

// Global variable
Eigen::VectorXd state_(18); //x
Eigen::MatrixXd weightedCovarSqrt_; // square root of (L+lamda)*P_k-1
Eigen::MatrixXd estimateErrorCovariance_(18,18); // P_k-1
std::vector<Eigen::VectorXd> sigmaPoints_;
std::vector<double> stateWeights_;
std::vector<double> covarWeights_;
double lambda_;
bool uncorrected_;
int flag;
/*test variable*/




enum StateMembers
{
  StateMemberX = 0,
  StateMemberY,
  StateMemberZ,
  StateMemberRoll,
  StateMemberPitch,
  StateMemberYaw,
  StateMemberVx,
  StateMemberVy,
  StateMemberVz,
  StateMemberVroll,
  StateMemberVpitch,
  StateMemberVyaw,
  StateMemberAx,
  StateMemberAy,
  StateMemberAz,
  StateMemberFx,
  StateMemberFy,
  StateMemberFz
};

bool checkMahalanobisThreshold(const Eigen::VectorXd &innovation,
                               const Eigen::MatrixXd &invCovariance,
                               const double nsigmas)
{
  double sqMahalanobis = innovation.dot(invCovariance * innovation);
  double threshold = nsigmas * nsigmas;

  if (sqMahalanobis >= threshold)
  {
    return false;
  }

  return true;
}

void initialize(){
  /*test variable*/
  /*
  svo_pose.pose.pose.position.x = 0.1;
  svo_pose.pose.pose.position.y = 0.2;
  svo_pose.pose.pose.position.z = 0.3;
  imu_data.linear_acceleration.x = 0.1;
  imu_data.linear_acceleration.y = 0.2;
  imu_data.linear_acceleration.z = 9.9;
  */
  /*test variable*/
  double alpha = 1e-3;
  double kappa = 0;
  double beta = 2;
  const int STATE_SIZE = 18;
  float sigmaCount = (STATE_SIZE << 1) +1; //2L + 1 = 37(18 states)
  sigmaPoints_.resize(sigmaCount, Eigen::VectorXd(STATE_SIZE));

  //Prepare constants
  //lamda,
  lambda_ = alpha * alpha * (STATE_SIZE + kappa) - STATE_SIZE;
  //ROS_INFO("lamda = %f", lambda_);
  stateWeights_.resize(sigmaCount);
  covarWeights_.resize(sigmaCount);

  // Wi_c, Wi_m
  stateWeights_[0] = lambda_ / (STATE_SIZE + lambda_);
  covarWeights_[0] = stateWeights_[0] + (1 - (alpha * alpha) + beta);
  sigmaPoints_[0].setZero();


  for (size_t i = 1; i < sigmaCount; ++i)
  {
    sigmaPoints_[i].setZero();
    stateWeights_[i] =  1 / (2 * (STATE_SIZE + lambda_));
    covarWeights_[i] = stateWeights_[i];
  }

  // Initialize Px,P_k-1
  estimateErrorCovariance_(0,0) = 1e-02;// x
  estimateErrorCovariance_(1,1) = 1e-02;// y
  estimateErrorCovariance_(2,2) = 1e-02;// z
  estimateErrorCovariance_(3,3) = 1e-02;// roll
  estimateErrorCovariance_(4,4) = 1e-02;// pitch
  estimateErrorCovariance_(5,5) = 1e-02;// yaw
  estimateErrorCovariance_(6,6) = 1e-02;// Vx
  estimateErrorCovariance_(7,7) = 1e-02;// Vy
  estimateErrorCovariance_(8,8) = 1e-02;// Vz
  estimateErrorCovariance_(9,9) = 1e-02;// Vroll
  estimateErrorCovariance_(10,10) = 1e-02;// Vpitch
  estimateErrorCovariance_(11,11) = 1e-02;// Vyaw
  estimateErrorCovariance_(12,12) = 1e-02;// Ax
  estimateErrorCovariance_(13,13) = 1e-02;// Ay
  estimateErrorCovariance_(14,14) = 1e-02;// Az
  estimateErrorCovariance_(15,15) = 1e-02;//Fx
  estimateErrorCovariance_(16,16) = 1e-02;//Fy
  estimateErrorCovariance_(17,17) = 1e-02;//Fz

  // Initialize state by using first measurement x_0
  state_.setZero();


}

double clamRotation(double rotation)
{
  const double PI = 3.141592653589793;
  const double TAU = 6.283185307179587;
  while (rotation > PI)
  {
    rotation -= TAU;
  }

  while (rotation < -PI)
  {
    rotation += TAU;
  }

  return rotation;
}

void quaternionToRPY(){
  /*
  imu_data.orientation.x = 0.1;
  imu_data.orientation.y = 0.05;
  imu_data.orientation.z = 0.1;
  imu_data.orientation.w = 1;*/
  if(imu_data.orientation.w == 0)
  {
    imu_data.orientation.w = 1;
    flag = 0;
  }
  if(imu_data.orientation.w != 0 && imu_data.orientation.w != 1)
    flag = 1;

  //ROS_INFO("flag = %d", flag);
  //ROS_INFO("imu = %f", imu_data.orientation.w);
  tf::Quaternion quat(imu_data.orientation.x, imu_data.orientation.y, imu_data.orientation.z, imu_data.orientation.w);
  double roll, pitch, yaw;
  tf::Matrix3x3(quat).getRPY(roll, pitch, yaw);

  geometry_msgs::Vector3 rpy;
  rpy.x = roll;
  rpy.y = pitch;
  rpy.z = yaw;
  state_[StateMemberRoll] = rpy.x;
  state_[StateMemberPitch] = rpy.y;
  state_[StateMemberYaw] = rpy.z;
  //ROS_INFO("roll = %f, pitch = %f, yaw = %f", state_[StateMemberRoll],state_[StateMemberPitch],state_[StateMemberYaw]);

}

void writeInMeasurement(){
  /*test
  svo_pose.pose.pose.position.x = 0.1;
  svo_pose.pose.pose.position.y = 0.2;
  svo_pose.pose.pose.position.z = 0.3;
  imu_data.linear_acceleration.x = 1.1;
  imu_data.linear_acceleration.y = 0.0;
  imu_data.linear_acceleration.z = 9.9 - 9.8;
  test*/
  measurement.measurement_.resize(18);
  measurement.measurement_[StateMemberX] = svo_pose.pose.pose.position.x ;
  measurement.measurement_[StateMemberY] = svo_pose.pose.pose.position.y ;
  measurement.measurement_[StateMemberZ] = svo_pose.pose.pose.position.z ;
  measurement.measurement_[StateMemberAx] = imu_data.linear_acceleration.x ;
  measurement.measurement_[StateMemberAy] = imu_data.linear_acceleration.y ;
  measurement.measurement_[StateMemberAz] = imu_data.linear_acceleration.z ;

  //ROS_INFO("meas_x = %f, mea_y = %f", measurement.measurement_[StateMemberX], measurement.measurement_[StateMemberY]);

}

void correct(){

  const int STATE_SIZE = 18;
  const double PI = 3.141592653589793;
  const double TAU = 6.283185307179587;

  //correct, calculate sigma points, if uncorrected = true ,than this loop won't be run.
  if (!uncorrected_)
  {
    // caculate square root of (L+lamda)*P_k-1
    weightedCovarSqrt_ = ((STATE_SIZE + lambda_) * estimateErrorCovariance_).llt().matrixL();
    // First sigma point is the current state
    sigmaPoints_[0] = state_;
    // Generate the sigma points
    // x_i = x + weightedCovarSqrt_ , i = 1, ..., L
    // x_i = x - weightedCovarSqrt_ , i = L+1, ..., 2L
    for (size_t sigmaInd = 0; sigmaInd < STATE_SIZE; ++sigmaInd)
     {
      sigmaPoints_[sigmaInd + 1] = state_ + weightedCovarSqrt_.col(sigmaInd);
      sigmaPoints_[sigmaInd + 1 + STATE_SIZE] = state_ - weightedCovarSqrt_.col(sigmaInd);
     }
  }


  // We don't want to update everything, so we need to build matrices that only update
  // the measured parts of our state vector

  // First, determine how many state vector values we're updating

  size_t updateSize = measurement.updatesize ;

  // Now set up the relevant matrices
  Eigen::VectorXd stateSubset(updateSize);                              // x (in most literature)
  Eigen::VectorXd measurementSubset(STATE_SIZE);                        // y
  Eigen::MatrixXd measurementCovarianceSubset(STATE_SIZE, STATE_SIZE);  // Py
  Eigen::MatrixXd stateToMeasurementSubset(STATE_SIZE, STATE_SIZE);     // H
  Eigen::MatrixXd kalmanGainSubset(STATE_SIZE, updateSize);             // K
  Eigen::VectorXd innovationSubset(updateSize);                         // y - Hx
  Eigen::VectorXd predictedMeasurement(updateSize);
  Eigen::VectorXd sigmaDiff(updateSize);
  Eigen::MatrixXd predictedMeasCovar(STATE_SIZE, STATE_SIZE);
  Eigen::MatrixXd crossCovar(STATE_SIZE, updateSize);

  std::vector<Eigen::VectorXd> sigmaPointMeasurements(sigmaPoints_.size(), Eigen::VectorXd(updateSize));


  stateSubset.setZero();
  measurementSubset.setZero();
  measurementCovarianceSubset.setZero();
  stateToMeasurementSubset.setZero();
  kalmanGainSubset.setZero();
  innovationSubset.setZero();
  predictedMeasurement.setZero();
  predictedMeasCovar.setZero();
  crossCovar.setZero();

  // Now build the sub-matrices from the full-sized matrices

  for (size_t i = 0; i < STATE_SIZE; ++i){
    measurementSubset(i) = measurement.measurement_[i];
    //stateSubset(i) = state_(i);
    //measurementCovarianceSubset(i,i) = measurement.covariance_(i,i);

   }

  // The state-to-measurement function, H, will now be a measurement_size x full_state_size
  // matrix, with ones in the (i, i) locations of the values to be updated
  stateToMeasurementSubset(0,0) = 1;
  stateToMeasurementSubset(1,1) = 1;
  stateToMeasurementSubset(2,2) = 1;
  stateToMeasurementSubset(3,3) = 0;
  stateToMeasurementSubset(4,4) = 0;
  stateToMeasurementSubset(5,5) = 0;
  stateToMeasurementSubset(6,6) = 0;
  stateToMeasurementSubset(7,7) = 0;
  stateToMeasurementSubset(8,8) = 0;
  stateToMeasurementSubset(9,9) = 0;
  stateToMeasurementSubset(10,10) = 0;
  stateToMeasurementSubset(11,11) = 0;
  stateToMeasurementSubset(12,12) = 1;
  stateToMeasurementSubset(13,13) = 1;
  stateToMeasurementSubset(14,14) = 1;
  stateToMeasurementSubset(15,15) = 0;
  stateToMeasurementSubset(16,16) = 0;
  stateToMeasurementSubset(17,17) = 0;

  //The measurecovariance subset

  measurementCovarianceSubset(0,0) = 0.01;
  measurementCovarianceSubset(1,1) = 0.01;
  measurementCovarianceSubset(2,2) = 0.01;
  measurementCovarianceSubset(12,12) = 0.01;
  measurementCovarianceSubset(13,13) = 0.01;
  measurementCovarianceSubset(14,14) = 0.01;
  measurementCovarianceSubset(3,3) = measurementCovarianceSubset(4,4) = measurementCovarianceSubset(5,5) = measurementCovarianceSubset(6,6) = measurementCovarianceSubset(7,7) = measurementCovarianceSubset(8,8) = measurementCovarianceSubset(9,9) = measurementCovarianceSubset(10,10) = measurementCovarianceSubset(11,11) = measurementCovarianceSubset(15,15) = measurementCovarianceSubset(16,16) = measurementCovarianceSubset(17,17) = 0.01;

  // (5) Generate sigma points, use them to generate a predicted measurement,y_k_hat-
  for (size_t sigmaInd = 0; sigmaInd < sigmaPoints_.size(); ++sigmaInd)
  {
    sigmaPointMeasurements[sigmaInd] = stateToMeasurementSubset * sigmaPoints_[sigmaInd];
    // y = sum of (wi*yi)
    predictedMeasurement.noalias() += stateWeights_[sigmaInd] * sigmaPointMeasurements[sigmaInd];
  }
  //ROS_INFO("predic_mea = %f", predictedMeasurement[0]);

  // (6) Use the sigma point measurements and predicted measurement to compute a predicted
  // measurement covariance matrix P_yy and a state/measurement cross-covariance matrix P_xy.
  for (size_t sigmaInd = 0; sigmaInd < sigmaPoints_.size(); ++sigmaInd)
  {
    sigmaDiff = sigmaPointMeasurements[sigmaInd] - predictedMeasurement;//Y(i)_k|k-1 - y_k_hat-
    predictedMeasCovar.noalias() += covarWeights_[sigmaInd] * (sigmaDiff * sigmaDiff.transpose());//P_y_k~_y_k_~
    crossCovar.noalias() += covarWeights_[sigmaInd] * ((sigmaPoints_[sigmaInd] - state_) * sigmaDiff.transpose());//P_x_k_y_k
  }
  ROS_INFO("predictedMeasCovar = %f", predictedMeasCovar(0,0));
  //ROS_INFO("crossCovar = %f",crossCovar(10,10));

  // (7) Compute the Kalman gain, making sure to use the actual measurement covariance: K = P_x_k_y_k * (P_y_k~_y_k_~ + R)^-1
  // kalman gain :https://dsp.stackexchange.com/questions/2347/how-to-understand-kalman-gain-intuitively
  Eigen::MatrixXd invInnovCov = (predictedMeasCovar + measurementCovarianceSubset).inverse();
  //Eigen::MatrixXd inv_test = predictedMeasCovar + measurementCovarianceSubset;
  //ROS_INFO("invInnovCov = %f", invInnovCov(0,0));
  kalmanGainSubset = crossCovar * invInnovCov;
  //ROS_INFO("kalmanGain = %f", kalmanGainSubset(5,5));


  // (8) Apply the gain to the difference between the actual and predicted measurements: x = x + K(y - y_hat)
  // y - y_hat
  //ROS_INFO("measure = %f",measurementSubset[0]);
  //ROS_INFO("predic_measure = %f", predictedMeasurement[0]);

  innovationSubset = (measurementSubset - predictedMeasurement);
  ROS_INFO("innovationSubset = %f", innovationSubset[0]);
  //Eigen::MatrixXd test = kalmanGainSubset * innovationSubset;
  //ROS_INFO("%f", test(0,0));
  //ROS_INFO("state = %f", state_[0]);

  // Wrap angles in the innovation
  while (innovationSubset(StateMemberRoll) < -PI)
   {
   innovationSubset(StateMemberRoll) += TAU;
   }

   while (innovationSubset(StateMemberRoll) > PI)
   {
    innovationSubset(StateMemberRoll) -= TAU;
   }

   while (innovationSubset(StateMemberYaw) < -PI)
    {
    innovationSubset(StateMemberYaw) += TAU;
    }

    while (innovationSubset(StateMemberYaw) > PI)
    {
     innovationSubset(StateMemberYaw) -= TAU;
    }

    while (innovationSubset(StateMemberPitch) < -PI)
     {
     innovationSubset(StateMemberPitch) += TAU;
     }

     while (innovationSubset(StateMemberPitch) > PI)
     {
      innovationSubset(StateMemberPitch) -= TAU;
     }
     double sqMahalanobis = innovationSubset.dot(invInnovCov * innovationSubset);
     //double threshold = 1 * 1;
     ROS_INFO("sq = %f", sqMahalanobis);

  // (8.1) Check Mahalanobis distance of innovation
  measurement.mahalanobisThresh_ = 8;
  if (checkMahalanobisThreshold(innovationSubset, invInnovCov, measurement.mahalanobisThresh_))
  {
    // x = x + K*(y - y_hat)
    state_.noalias() += kalmanGainSubset * innovationSubset;
    //ROS_INFO("state = %f", state_[0]);

    filterd.pose.pose.position.x = state_[0];
    filterd.pose.pose.position.y = state_[1];
    filterd.pose.pose.position.z = state_[2];
    filterd.twist.twist.linear.x = state_[6];
    filterd.twist.twist.linear.y = state_[7];
    filterd.twist.twist.linear.z = state_[8];
    //ROS_INFO("filtered_x = %f", filterd.pose.pose.position.x);



    // (9) Compute the new estimate error covariance P = P - (K * P_yy * K')
    estimateErrorCovariance_.noalias() -= (kalmanGainSubset * predictedMeasCovar * kalmanGainSubset.transpose());

    //wrapStateAngles();
    state_(StateMemberRoll) = clamRotation(state_(StateMemberRoll));
    state_(StateMemberYaw) = clamRotation(state_(StateMemberYaw));
    state_(StateMemberPitch) = clamRotation(state_(StateMemberPitch));

    // Mark that we need to re-compute sigma points for successive corrections
    uncorrected_ = false;
  }

}

void predict(const double referenceTime, const double delta)
{
  Eigen::MatrixXd transferFunction_(18,18);
  double m = 1;
  const int STATE_SIZE = 18;



  double roll = state_(StateMemberRoll);
  double pitch = state_(StateMemberPitch);
  double yaw = state_(StateMemberYaw);

  // We'll need these trig calculations a lot.
  double sp = ::sin(pitch);
  double cp = ::cos(pitch);

  double sr = ::sin(roll);
  double cr = ::cos(roll);

  double sy = ::sin(yaw);
  double cy = ::cos(yaw);
  //ROS_INFO("sp = %f, cp = %f, sy = %f", sp , cp, sy);
  // Prepare the transfer function
  transferFunction_(0,0) = transferFunction_(1,1) = transferFunction_(2,2) = transferFunction_(3,3) = transferFunction_(4,4) = transferFunction_(5,5) = transferFunction_(6,6) = transferFunction_(7,7) = transferFunction_(8,8) = transferFunction_(9,9) = transferFunction_(10,10) = transferFunction_(11,11) = transferFunction_(12,12) = transferFunction_(13,13) = transferFunction_(14,14) = 1;
  transferFunction_(StateMemberX, StateMemberVx) = cy * cp * delta;
  transferFunction_(StateMemberX, StateMemberVy) = (cy * sp * sr - sy * cr) * delta;
  transferFunction_(StateMemberX, StateMemberVz) = (cy * sp * cr + sy * sr) * delta;
  transferFunction_(StateMemberX, StateMemberAx) = 0.5 * transferFunction_(StateMemberX, StateMemberVx) * delta;
  transferFunction_(StateMemberX, StateMemberAy) = 0.5 * transferFunction_(StateMemberX, StateMemberVy) * delta;
  transferFunction_(StateMemberX, StateMemberAz) = 0.5 * transferFunction_(StateMemberX, StateMemberVz) * delta;
  transferFunction_(StateMemberY, StateMemberVx) = sy * cp * delta;
  transferFunction_(StateMemberY, StateMemberVy) = (sy * sp * sr + cy * cr) * delta;
  transferFunction_(StateMemberY, StateMemberVz) = (sy * sp * cr - cy * sr) * delta;
  transferFunction_(StateMemberY, StateMemberAx) = 0.5 * transferFunction_(StateMemberY, StateMemberVx) * delta;
  transferFunction_(StateMemberY, StateMemberAy) = 0.5 * transferFunction_(StateMemberY, StateMemberVy) * delta;
  transferFunction_(StateMemberY, StateMemberAz) = 0.5 * transferFunction_(StateMemberY, StateMemberVz) * delta;
  transferFunction_(StateMemberZ, StateMemberVx) = -sp * delta;
  transferFunction_(StateMemberZ, StateMemberVy) = cp * sr * delta;
  transferFunction_(StateMemberZ, StateMemberVz) = cp * cr * delta;
  transferFunction_(StateMemberZ, StateMemberAx) = 0.5 * transferFunction_(StateMemberZ, StateMemberVx) * delta;
  transferFunction_(StateMemberZ, StateMemberAy) = 0.5 * transferFunction_(StateMemberZ, StateMemberVy) * delta;
  transferFunction_(StateMemberZ, StateMemberAz) = 0.5 * transferFunction_(StateMemberZ, StateMemberVz) * delta;
  transferFunction_(StateMemberRoll, StateMemberVroll) = transferFunction_(StateMemberX, StateMemberVx);
  transferFunction_(StateMemberRoll, StateMemberVpitch) = transferFunction_(StateMemberX, StateMemberVy);
  transferFunction_(StateMemberRoll, StateMemberVyaw) = transferFunction_(StateMemberX, StateMemberVz);
  transferFunction_(StateMemberPitch, StateMemberVroll) = transferFunction_(StateMemberY, StateMemberVx);
  transferFunction_(StateMemberPitch, StateMemberVpitch) = transferFunction_(StateMemberY, StateMemberVy);
  transferFunction_(StateMemberPitch, StateMemberVyaw) = transferFunction_(StateMemberY, StateMemberVz);
  transferFunction_(StateMemberYaw, StateMemberVroll) = transferFunction_(StateMemberZ, StateMemberVx);
  transferFunction_(StateMemberYaw, StateMemberVpitch) = transferFunction_(StateMemberZ, StateMemberVy);
  transferFunction_(StateMemberYaw, StateMemberVyaw) = transferFunction_(StateMemberZ, StateMemberVz);
  transferFunction_(StateMemberVx, StateMemberAx) = delta;
  transferFunction_(StateMemberVy, StateMemberAy) = delta;
  transferFunction_(StateMemberVz, StateMemberAz) = delta;
  transferFunction_(StateMemberFx,StateMemberAx) = m*cy * cp;
  transferFunction_(StateMemberFx,StateMemberAy) = m*(cy * sp * sr - sy * cr);
  transferFunction_(StateMemberFx,StateMemberAz) = m*(cy * sp * cr + sy * sr);
  transferFunction_(StateMemberFy,StateMemberAx) = m*sy * cp;
  transferFunction_(StateMemberFy,StateMemberAy) = m*(sy * sp * sr + cy * cr);
  transferFunction_(StateMemberFy,StateMemberAz) = m*(sy * sp * cr - cy * sr);
  transferFunction_(StateMemberFz,StateMemberAx) = m*(-sp);
  transferFunction_(StateMemberFz,StateMemberAy) = m*cp * sr;
  transferFunction_(StateMemberFz,StateMemberAz) = m*cp * cr ;
  // (1) Take the square root of a small fraction of the estimateErrorCovariance_ using LL' decomposition
  // caculate square root of (L+lamda)*P_k-1
  // This will be a diagonal matrix (18*18)
  weightedCovarSqrt_ = ((STATE_SIZE + lambda_) * estimateErrorCovariance_).llt().matrixL();
  //ROS_INFO("%f", weightedCovarSqrt_(0,0));

  // (2) Compute sigma points *and* pass them through the transfer function to save
  // the extra loop

  // First sigma point(through transferfunction) is the current state
  // x_k|k-1(0)
  // sigmaPoint_[0][0~14]
  //ROS_INFO("state_x = %f", state_[0]);
  sigmaPoints_[0] = transferFunction_ * state_;
  //ROS_INFO("%f", sigmaPoints_[0][0]);



  // Next STATE_SIZE sigma points are state + weightedCovarSqrt_[ith column]
  // STATE_SIZE sigma points after that are state - weightedCovarSqrt_[ith column]
  for (size_t sigmaInd = 0; sigmaInd < STATE_SIZE; ++sigmaInd)
  {
    sigmaPoints_[sigmaInd + 1] = transferFunction_ * (state_ + weightedCovarSqrt_.col(sigmaInd));
    sigmaPoints_[sigmaInd + 1 + STATE_SIZE] = transferFunction_ * (state_ - weightedCovarSqrt_.col(sigmaInd));
  }
  //ROS_INFO("sigma = %f", sigmaPoints_[2][1]);



  // (3) Sum the weighted sigma points to generate a new state prediction
  // x_k_hat- = w_im * x_k|k-1
  state_.setZero();
  for (size_t sigmaInd = 0; sigmaInd < sigmaPoints_.size(); ++sigmaInd)
  {
    state_.noalias() += stateWeights_[sigmaInd] * sigmaPoints_[sigmaInd];
  }
  //ROS_INFO("state = %f",state_[0]);


  // (4) Now us the sigma points and the predicted state to compute a predicted covariance P_k-
  estimateErrorCovariance_.setZero();

  Eigen::VectorXd sigmaDiff(STATE_SIZE);
  for (size_t sigmaInd = 0; sigmaInd < sigmaPoints_.size(); ++sigmaInd)
  {
    sigmaDiff = (sigmaPoints_[sigmaInd] - state_);
    //ROS_INFO("sigmapoint = %f", sigmaPoints_[0][0]);
    //ROS_INFO("sigmaDiff = %f", sigmaDiff[0]);
    estimateErrorCovariance_.noalias() += covarWeights_[sigmaInd] * (sigmaDiff * sigmaDiff.transpose());
  }
  //ROS_INFO("estimateErrorCov = %f", estimateErrorCovariance_(0,0));
  // Mark that we can keep these sigma points
      uncorrected_ = true;

}


int main(int argc, char **argv)
{
  ros::init(argc, argv, "ukf_estimate");
  ros::NodeHandle nh;
  ros::Subscriber svo_sub = nh.subscribe<geometry_msgs::PoseWithCovarianceStamped>("/svo/pose_imu", 10, svo_cb);
  ros::Subscriber imu_sub = nh.subscribe<sensor_msgs::Imu>("/drone2/mavros/imu/data", 10, imu_cb);
  ros::Publisher filtered_pub = nh.advertise<nav_msgs::Odometry>("/filtered/odom",10);
  initialize();
  ros::Rate rate(50);
  while(ros::ok()){
    filterd.header.stamp = ros::Time::now();

    quaternionToRPY();
    if(flag ==1)
    {
    writeInMeasurement();
    predict(1,0.01);
    correct();
    }
    filtered_pub.publish(filterd);
    ros::spinOnce();
    rate.sleep();


  }
}
