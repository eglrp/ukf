/**
 * @file offb_main.cpp
 * @author Julian Oes <julian@oes.ch>
 * @license BSD 3-clause
 *
 * @brief ROS node to do offboard control of PX4 through MAVROS.
 *
 * Initial code taken from http://dev.px4.io/ros-mavros-offboard.html
 */


#include <ros/ros.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/TwistStamped.h>
#include <mavros_msgs/CommandBool.h>
#include <mavros_msgs/SetMode.h>
#include <mavros_msgs/State.h>
#include <cstdio>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <UKF/output.h>
#include <tf/transform_datatypes.h>
#include <geometry_msgs/Point.h>
#include <sensor_msgs/Imu.h>
#include <eigen3/Eigen/Dense>
#include "lpf2.h"
#include <UKF/output.h>
//
double err_sumx , err_sumy , err_sumz;
double err_diffx , err_diffy , err_diffz;
double last_errx,last_erry,last_errz;
bool force_control;
bool velocity_forward;
bool velocity_backward;
bool velocity_upward;
bool velocity_downward;
bool velocity_leftward;
bool velocity_rightward;
bool velocity_zero;
bool velocity_circle;
double theta_c;
int flag1 = 0;
geometry_msgs::Point trigger;
geometry_msgs::Point force_error, pos_error;
geometry_msgs::Point connector_vel;
const float pi = 3.1415926;
float imu_roll, imu_pitch, imu_yaw;
float drone2_ax, drone2_ay, drone2_az;
int flag, flag2;
float KPx = 1.5;
float KPy = 1.5;
float KPz = 1.4;
float KProll = 1;
float KDx = 0.01;
float KDy = 0.01;
float KDz = 0.01;
bool landing;

int current_state_zone = 0;
int last_state_zone = 0;
int controller_state;
const double u_bound = 1.5;
const double l_bound = -1.5;

double time_now;
double lastime;
double last_updat_time;
lpf2  lpFLx(10,0.02);
lpf2  lpFFx(10,0.02);
lpf2  lpFLy(10,0.02);
lpf2  lpFFy(10,0.02);
using namespace std;

enum zone{
    positive_zone = 5,
    zero_zone = 0 ,
    negative_zone = -5
};

enum control_state{
  positive_engaged = 5,
  disengaged = 0,
  negative_engaged = -5
};

typedef struct vir
{
    float roll;
    float x;
    float y;
    float z;
};
geometry_msgs::TwistStamped drone2_vel;
void drone2_vel_cb(const geometry_msgs::TwistStamped::ConstPtr& msg) {
    drone2_vel = *msg;
}
void connector_vel_cb(const geometry_msgs::Point::ConstPtr& msg) {
    connector_vel = *msg;
}
geometry_msgs::Point leader_force;
void leader_force_cb(const geometry_msgs::Point::ConstPtr& msg) {
    leader_force = *msg;
}
sensor_msgs::Imu imu2, imu3;
void imu2_cb(const sensor_msgs::Imu::ConstPtr& msg){
  imu2 = *msg;
}
void imu3_cb(const sensor_msgs::Imu::ConstPtr& msg){
  imu3 = *msg;
}
UKF::output follower_force;
void follower_force_cb(const UKF::output::ConstPtr& msg){
  follower_force = *msg;
}
UKF::output leader_force_est;
void leader_force_est_cb(const UKF::output::ConstPtr& msg){
  leader_force_est = *msg;
}

mavros_msgs::State current_state, current_state2;
void state_cb(const mavros_msgs::State::ConstPtr& msg) {
    current_state = *msg;
}
void state_cb2(const mavros_msgs::State::ConstPtr& msg) {
    current_state2 = *msg;
}
geometry_msgs::PoseStamped host_mocap, host_mocap2, contact_pf, contact_pl;
void host_pos(const geometry_msgs::PoseStamped::ConstPtr& msg)
{
  host_mocap = *msg;
}
void host_pos2(const geometry_msgs::PoseStamped::ConstPtr& msg)
{
  host_mocap2 = *msg;
}
void contact_pf_cb(const geometry_msgs::PoseStamped::ConstPtr& msg)
{
  contact_pf = *msg;
}
void contact_pl_cb(const geometry_msgs::PoseStamped::ConstPtr& msg)
{
  contact_pl = *msg;
}
float qua2eul(geometry_msgs::PoseStamped& host_mocap)
{
    float pitch,yaw,roll,qx2,qy2,qz2,qw2;
    qx2=(host_mocap.pose.orientation.x)*(host_mocap.pose.orientation.x);
    qy2=(host_mocap.pose.orientation.y)*(host_mocap.pose.orientation.y);
    qz2=(host_mocap.pose.orientation.z)*(host_mocap.pose.orientation.z);
    qw2=(host_mocap.pose.orientation.w)*(host_mocap.pose.orientation.w);
    roll = atan2(2*host_mocap.pose.orientation.z*host_mocap.pose.orientation.w+2*host_mocap.pose.orientation.x*host_mocap.pose.orientation.y , 1 - 2*qy2 - 2*qz2);
    //roll = asin(2*host_mocap.pose.orientation.x*host_mocap.pose.orientation.y + 2*host_mocap.pose.orientation.z*host_mocap.pose.orientation.w);
    //pitch = atan2(2*host_mocap.pose.orientation.x*host_mocap.pose.orientation.w-2*host_mocap.pose.orientation.y*host_mocap.pose.orientation.z , 1 - 2*qx2 - 2*qz2);
  //ROS_INFO("eul: %.3f, %.3f, %.3f", pitch/pi*180, yaw/pi*180, roll/pi*180);

    return roll;
}

void quaternionToRPY(){
   //imu orientation

  if(imu2.orientation.w == 0)
  {
    imu2.orientation.w = 1;
    flag = 0;
  }
  if(imu2.orientation.w != 0 && imu2.orientation.w != 1){
    flag = 1;
  }

  tf::Quaternion quat(imu2.orientation.x, imu2.orientation.y, imu2.orientation.z, imu2.orientation.w);

  if(host_mocap2.pose.orientation.w == 0)
  {
    host_mocap2.pose.orientation.w = 1;
    flag2 = 0;
  }
  if(host_mocap2.pose.orientation.w != 0 && host_mocap2.pose.orientation.w != 1)
    flag2 = 1;

  tf::Quaternion quat1(host_mocap2.pose.orientation.x, host_mocap2.pose.orientation.y, host_mocap2.pose.orientation.z, host_mocap2.pose.orientation.w);

  double roll, pitch, yaw;
  double yaw_bias;
  double roll_mocap, pitch_mocap, yaw_mocap;
  tf::Matrix3x3(quat).getRPY(roll, pitch, yaw);
  tf::Matrix3x3(quat1).getRPY(roll_mocap, pitch_mocap, yaw_mocap);

  geometry_msgs::Vector3 rpy;
  geometry_msgs::Vector3 rpy_mocap;
  rpy.x = roll;
  rpy.y = pitch;
  rpy.z = yaw;

  rpy_mocap.x = roll_mocap;
  rpy_mocap.y = pitch_mocap;
  rpy_mocap.z = yaw_mocap;

  imu_roll = rpy.x;
  imu_pitch = rpy.y;
  imu_yaw = rpy_mocap.z;

}

void writeInMeasurement(){


  float roll, pitch , yaw;
  const float a_g = 9.80665;
  const float imu_ax_bias = 0.006944;
  const float imu_ay_bias = 0.066419;
  const float imu_az_bias = -0.164420;
  Eigen::Matrix3f Rx, Ry, Rz;
  Eigen::Vector3f a_g_inertial;
  Eigen::Vector3f a_g_body;
  Eigen::Vector3f drone2_body;
  Eigen::Vector3f drone2_inertial;
  Rx.setZero();
  Ry.setZero();
  Rz.setZero();
  a_g_inertial.setZero();
  drone2_body.setZero();
  drone2_inertial.setZero();
  a_g_inertial(0) = 0;
  a_g_inertial(1) = 0;
  a_g_inertial(2) = a_g;
  roll = imu_roll;
  pitch = imu_pitch;
  yaw = imu_yaw;
  Rx(0,0) = 1;
  Rx(1,0) = 0;
  Rx(2,0) = 0;
  Rx(0,1) = 0;
  Rx(1,1) = cos(roll);
  Rx(1,2) = -sin(roll);
  Rx(0,2) = 0;
  Rx(2,1) = sin(roll);
  Rx(2,2) = cos(roll);

  Ry(0,0) = cos(pitch);
  Ry(1,0) = 0;
  Ry(2,0) = sin(pitch);
  Ry(0,1) = 0;
  Ry(1,1) = 1;
  Ry(1,2) = 0;
  Ry(0,2) = -sin(pitch);
  Ry(2,1) = 0;
  Ry(2,2) = cos(pitch);

  Rz(0,0) = cos(yaw);
  Rz(1,0) = -sin(yaw);
  Rz(2,0) = 0;
  Rz(0,1) = sin(yaw);
  Rz(1,1) = cos(yaw);
  Rz(1,2) = 0;
  Rz(0,2) = 0;
  Rz(2,1) = 0;
  Rz(2,2) = 1;

  a_g_body = Ry*Rx*Rz*a_g_inertial;


  drone2_body(0) = -(imu2.linear_acceleration.x - imu_ax_bias - a_g_body(0));
  drone2_body(1) = -(imu2.linear_acceleration.y - imu_ay_bias + a_g_body(1));
  drone2_body(2) = -(imu2.linear_acceleration.z - imu_az_bias - a_g_body(2));
  //ROS_INFO("ax = %f, ay = %f, az = %f", drone2_ax, drone2_ay, drone2_az);
  drone2_inertial = (Ry*Rx*Rz).inverse()*drone2_body;
  drone2_ax = drone2_inertial(0);
  drone2_ay = drone2_inertial(1);
  drone2_az = drone2_inertial(2);
}

void follow(vir& vir, geometry_msgs::PoseStamped& host_mocap, geometry_msgs::TwistStamped* vs, float dis_x, float dis_y)
{
float errx, erry, errz, err_roll;
float ux, uy, uz, uroll;
//float dis_x = 0, dis_y = -0.5;
float local_x, local_y;
double pcl_x, pcl_y, pcl_z;
double pcf_x, pcf_y, pcf_z;
double pg_x, pg_y, pg_z;
double zx, zy, zz;
double fx, fy, fz;
double FL_x, FL_y;

FL_x = lpFLx.filter(-leader_force_est.force.x);
FL_y = lpFLy.filter(-leader_force_est.force.y);

pcl_x = contact_pl.pose.position.x;
pcl_y = contact_pl.pose.position.y;
pcl_z = contact_pl.pose.position.z;

pcf_x = contact_pf.pose.position.x;
pcf_y = contact_pf.pose.position.y;
pcf_z = contact_pf.pose.position.z;

pg_x = 0.5*(pcl_x + pcf_x);
pg_y = 0.5*(pcl_y + pcf_y);
pg_z = 0.5*(pcl_z + pcf_z);

zx = (pcl_x - pg_x);
zy = (pcl_y - pg_y);
zz = (pcl_z - pg_z);

fx = -zy;
fy = zx;




local_x = cos(vir.roll)*dis_x+sin(vir.roll)*dis_y;
local_y = -sin(vir.roll)*dis_x+cos(vir.roll)*dis_y;

errx = vir.x - host_mocap.pose.position.x - local_x;
erry = vir.y - host_mocap.pose.position.y - local_y;
errz = vir.z - host_mocap.pose.position.z - 0;
err_roll = vir.roll - qua2eul(host_mocap);
if(err_roll>pi)
err_roll = err_roll - 2*pi;
else if(err_roll<-pi)
err_roll = err_roll + 2*pi;




//ROS_INFO("err_roll: %.3f",err_roll);

ux = KPx*errx;
uy = KPy*erry;
uz = KPz*errz;
uroll = KProll*err_roll;

if(force_control){
  ux = fx;
  uy = fy;
}

if(velocity_leftward){
  uy = 0.8;
}
if(velocity_rightward){
  uy = -0.8;
}
if(velocity_zero){
  ux = 0;
  uy = 0;

}

vs->twist.linear.x = ux;
vs->twist.linear.y = uy;
vs->twist.linear.z = uz;
vs->twist.angular.z = uroll;

}


void follow_force(vir& vir, geometry_msgs::PoseStamped& host_mocap, geometry_msgs::TwistStamped* vs, float dis_x, float dis_y)
{
float errx, erry, errz, err_roll;
float ux, uy, uz, uroll;
//float dis_x = 0, dis_y = -0.5;
float local_x, local_y;
float err_fx, err_fy, err_fz;
float err_ax, err_ay, err_az;
double FF_x, FF_y, FF_z;
const float cx = 1;
const float cy = 1;
const float cz = 1;
double FLx_filt, FFx_filt, lpf2_fz;
const double dt = 0.02;
const float force_threshold = 1.5;
double drone2_velx;

double pcl_x, pcl_y, pcl_z;
double pcf_x, pcf_y, pcf_z;
double pg_x, pg_y, pg_z;
double zx, zy, zz;
double fx, fy, fz;

pcl_x = contact_pl.pose.position.x;
pcl_y = contact_pl.pose.position.y;
pcl_z = contact_pl.pose.position.z;

pcf_x = contact_pf.pose.position.x;
pcf_y = contact_pf.pose.position.y;
pcf_z = contact_pf.pose.position.z;

pg_x = 0.5*(pcl_x + pcf_x);
pg_y = 0.5*(pcl_y + pcf_y);
pg_z = 0.5*(pcl_z + pcf_z);

zx = (pcf_x - pg_x);
zy = (pcf_y - pg_y);
zz = (pcf_z - pg_z);

fx = -zy;
fy = zx;

FF_x = lpFFx.filter(-follower_force.force.x);
FF_y = lpFFy.filter(-follower_force.force.y);

drone2_velx = drone2_vel.twist.linear.x;
err_diffx = 0;
err_diffy = 0;
err_diffz = 0;
local_x = cos(vir.roll)*dis_x+sin(vir.roll)*dis_y;
local_y = -sin(vir.roll)*dis_x+cos(vir.roll)*dis_y;

errx = vir.x - host_mocap.pose.position.x - local_x;
erry = vir.y - host_mocap.pose.position.y - local_y;
errz = vir.z - host_mocap.pose.position.z - 0;
err_roll = vir.roll - qua2eul(host_mocap);
if(err_roll>pi)
err_roll = err_roll - 2*pi;
else if(err_roll<-pi)
err_roll = err_roll + 2*pi;
FF_x = lpFFx.filter(-follower_force.force.x);
FF_y = lpFFy.filter(-follower_force.force.y);

ux = KPx*errx;
uy = KPy*erry;
uz = KPz*errz;
uroll = KProll*err_roll;

if(force_control){
  ux = fx;
  uy = fy;
}

if(velocity_zero){
  ux = 0;
  uy = 0;
}
ROS_INFO("ux = %f", ux);

vs->twist.linear.x = ux;
vs->twist.linear.y = uy;
vs->twist.linear.z = uz;
vs->twist.angular.z = uroll;

}

/*
 * Taken from
 * http://stackoverflow.com/questions/421860/capture-characters-from-standard-input-without-waiting-for-enter-to-be-pressed
 *
 * @return the character pressed.
 */
char getch()
{
    int flags = fcntl(0, F_GETFL, 0);
    fcntl(0, F_SETFL, flags | O_NONBLOCK);

    char buf = 0;
    struct termios old = {0};
    if (tcgetattr(0, &old) < 0) {
        perror("tcsetattr()");
    }
    old.c_lflag &= ~ICANON;
    old.c_lflag &= ~ECHO;
    old.c_cc[VMIN] = 1;
    old.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSANOW, &old) < 0) {
        perror("tcsetattr ICANON");
    }
    if (read(0, &buf, 1) < 0) {
        //perror ("read()");
    }
    old.c_lflag |= ICANON;
    old.c_lflag |= ECHO;
    if (tcsetattr(0, TCSADRAIN, &old) < 0) {
        perror ("tcsetattr ~ICANON");
    }
    return (buf);
}

/*
 * Call main using `rosrun offb offb_main`.
 */
int main(int argc, char **argv)
{
    ros::init(argc, argv, "leader_follower_force");
    ros::NodeHandle nh;

    ros::Subscriber state_sub = nh.subscribe<mavros_msgs::State>
                                ("drone3/mavros/state", 1, state_cb);
    //ros::Subscriber imu_sub = nh.subscribe<sensor_msgs::Imu>("drone3/mavros/imu/data", 2, imu3_cb);
    ros::Publisher local_pos_pub = nh.advertise<geometry_msgs::PoseStamped>
                                   ("drone3/mavros/setpoint_position/local", 1);
    //ros::Publisher mocap_pos_pub = nh.advertise<geometry_msgs::PoseStamped>
    //                               ("drone3/mavros/mocap/pose", 1);
    ros::ServiceClient arming_client = nh.serviceClient<mavros_msgs::CommandBool>
                                       ("drone3/mavros/cmd/arming");
    ros::ServiceClient set_mode_client = nh.serviceClient<mavros_msgs::SetMode>
                                         ("drone3/mavros/set_mode");
    ros::Subscriber host_sub = nh.subscribe<geometry_msgs::PoseStamped>("/vrpn_client_node/RigidBody3/pose", 1, host_pos);

    ros::Publisher local_vel_pub = nh.advertise<geometry_msgs::TwistStamped>("drone3/mavros/setpoint_velocity/cmd_vel", 5);


    ros::Subscriber state_sub2 = nh.subscribe<mavros_msgs::State>
                                ("drone2/mavros/state", 1, state_cb2);
    ros::Subscriber imu_sub2 = nh.subscribe<sensor_msgs::Imu>("drone2/mavros/imu/data", 1, imu2_cb);
    ros::Publisher local_pos_pub2 = nh.advertise<geometry_msgs::PoseStamped>
                                   ("drone2/mavros/setpoint_position/local", 1);
    //ros::Publisher mocap_pos_pub2 = nh.advertise<geometry_msgs::PoseStamped>
    //                               ("drone2/mavros/mocap/pose", 1);
    ros::ServiceClient arming_client2 = nh.serviceClient<mavros_msgs::CommandBool>
                                       ("drone2/mavros/cmd/arming");
    ros::ServiceClient set_mode_client2 = nh.serviceClient<mavros_msgs::SetMode>
                                         ("drone2/mavros/set_mode");
    ros::Subscriber drone2_vel_sub = nh.subscribe<geometry_msgs::TwistStamped>("drone2/mavros/local_position/velocity", 1, drone2_vel_cb);
    ros::Subscriber host_sub2 = nh.subscribe<geometry_msgs::PoseStamped>("/vrpn_client_node/RigidBody2/pose", 1, host_pos2);
    ros::Publisher local_vel_pub2 = nh.advertise<geometry_msgs::TwistStamped>("drone2/mavros/setpoint_velocity/cmd_vel", 5);

    ros::Subscriber leader_force_sub = nh.subscribe<geometry_msgs::Point>("/leader_force", 2, leader_force_cb);
    ros::Subscriber follower_force_sub = nh.subscribe<UKF::output>("/follower_ukf/output", 1, follower_force_cb);
    ros::Subscriber leader_force_est_sub = nh.subscribe<UKF::output>("/leader_ukf/output", 1, leader_force_est_cb);
    ros::Subscriber connector_vel_sub = nh.subscribe<geometry_msgs::Point>("/connector/velocity", 1, connector_vel_cb);
    ros::Publisher force_error_pub = nh.advertise<geometry_msgs::Point>("/force_error", 1);
    ros::Publisher pos_error_pub = nh.advertise<geometry_msgs::Point>("/pos_error", 1);
    ros::Publisher trigger_pub = nh.advertise<geometry_msgs::Point>("/trigger", 1);

    ros::Subscriber contact_pf_sub = nh.subscribe<geometry_msgs::PoseStamped>("/vrpn_client_node/RigidBody1/pose", 1, contact_pf_cb);
    ros::Subscriber contact_pl_sub = nh.subscribe<geometry_msgs::PoseStamped>("/vrpn_client_node/RigidBody4/pose", 1, contact_pl_cb);


    // The setpoint publishing rate MUST be faster than 2Hz.
    //ros::AsyncSpinner spinner(10);
    //spinner.start();
    ros::Rate rate(50);
    landing = false;
    force_control = false;
    velocity_forward = false;
    velocity_backward = false;
    velocity_zero = false;
    velocity_circle = false;
    velocity_leftward = false;
    velocity_rightward = false;
    velocity_upward = false;
    velocity_downward = false;
    lpf2  lpf2x(6,0.02);
    lpf2  lpf2y(6,0.02);
    // Wait for FCU connection.
    while (ros::ok() && current_state.connected && current_state2.connected) {
  //mocap_pos_pub.publish(host_mocap);
  //mocap_pos_pub2.publish(host_mocap2);
        ros::spinOnce();
        rate.sleep();
        ROS_INFO("ok");
    }


    geometry_msgs::PoseStamped pose;
    geometry_msgs::TwistStamped vs, vs2;
  vir vir1, vir2;

    vs.twist.linear.x = 0;
    vs.twist.linear.y = 0;
    vs.twist.linear.z = 0;
    vs.twist.angular.x = 0;
    vs.twist.angular.y = 0;
    vs.twist.angular.z = 0;

    vs2.twist.linear.x = 0;
    vs2.twist.linear.y = 0;
    vs2.twist.linear.z = 0;
    vs2.twist.angular.x = 0;
    vs2.twist.angular.y = 0;
    vs2.twist.angular.z = 0;
    vir1.x = 0.8;
    vir1.y = 0;
    vir1.z = 0.7;
    vir1.roll = 0;

    vir2.x = -0.8;
    vir2.y = 0;
    vir2.z = 0.7;
    vir2.roll = 0;
    //send a few setpoints before starting
   for(int i = 100; ros::ok() && i > 0; --i){
        local_vel_pub.publish(vs);
        local_vel_pub2.publish(vs2);
    //mocap_pos_pub.publish(host_mocap);
    //mocap_pos_pub2.publish(host_mocap2);
    ros::spinOnce();
        rate.sleep();
    }

    mavros_msgs::SetMode offb_set_mode, offb_set_mode2;
    offb_set_mode.request.custom_mode = "OFFBOARD";
    offb_set_mode2.request.custom_mode = "OFFBOARD";

    mavros_msgs::CommandBool arm_cmd, arm_cmd2;
    arm_cmd.request.value = true;
    arm_cmd2.request.value = true;

    ros::Time last_request = ros::Time::now();
    ros::Time last_request2 = ros::Time::now();
  //ros::Time last_request(0);
    //spinner.stop();
    while (ros::ok()) {
      quaternionToRPY();
      writeInMeasurement();
      //spinner.start();
  //mocap_pos_pub.publish(host_mocap);
  //mocap_pos_pub2.publish(host_mocap2);
        if (current_state.mode != "OFFBOARD" &&
                (ros::Time::now() - last_request > ros::Duration(5.0))) {
            if( set_mode_client.call(offb_set_mode) &&
                    offb_set_mode.response.mode_sent) {
                ROS_INFO("Offboard enabled");
            }
            last_request = ros::Time::now();
        } else {

            if (!current_state.armed &&
                    (ros::Time::now() - last_request > ros::Duration(5.0))) {
                if( arming_client.call(arm_cmd) &&
                        arm_cmd.response.success) {
                    ROS_INFO("Vehicle armed");
                }
                last_request = ros::Time::now();
            }
        }

        if (current_state2.mode != "OFFBOARD" &&
                (ros::Time::now() - last_request2 > ros::Duration(5.0))) {
            if( set_mode_client2.call(offb_set_mode2) &&
                    offb_set_mode2.response.mode_sent) {
                ROS_INFO("Offboard enabled2");
            }
            last_request2 = ros::Time::now();
        } else {

            if (!current_state2.armed &&
                    (ros::Time::now() - last_request2 > ros::Duration(5.0))) {
                if( arming_client2.call(arm_cmd2) &&
                        arm_cmd2.response.success) {
                    ROS_INFO("Vehicle armed2");
                }
                last_request2 = ros::Time::now();
            }
        }


  //take off

  /*if(host_mocap.pose.position.z>=0.5 && flag==0)
  {
    mocap_pos_pub.publish(host_mocap);
    vs.twist.linear.x = 0;
    vs.twist.linear.y = 0;
    vs.twist.linear.z = 0;
    vs.twist.angular.x = 0;
    vs.twist.angular.y = 0;
    vs.twist.angular.z = 0;
    flag = 1;
  }*/



        int c = getch();
  //ROS_INFO("C: %d",c);
        if (c != EOF) {
            switch (c) {
            case 65:    // key up
                vir1.z += 0.05;
                //vir2.z += 0.05;
                break;
            case 66:    // key down
                vir1.z += -0.05;
                //vir2.z += -0.05;
                break;
            case 56:    // key up,Num 8
                vir2.z += 0.05;
                break;
            case 53:    // key down,Num 5
                vir2.z += -0.05;
                break;
            case 67:    // key CW(->)
                vir1.roll -= 0.05;
                break;
            case 68:    // key CCW(<-)
                vir1.roll += 0.05;
                break;
            case 119:    // key foward
                vir1.x += 0.05;
                break;
            case 120:    // key back
                vir1.x += -0.05;
                break;
            case 97:    // key left
                vir1.y += 0.05;
                break;
            case 100:    // key right
                vir1.y -= 0.05;
                break;
            case 49: // tracking force (keyboard "1")
                force_control = true;
                break;
            case 50: // 2
                velocity_forward = true;
                velocity_backward = false;
                velocity_zero = false;
                break;
            case 51: // 3
                velocity_zero = true;
                velocity_forward = false;
                velocity_backward = false;
                velocity_upward = false;
                velocity_downward = false;
                velocity_rightward = false;
                velocity_leftward = false;
                break;
            case 52: // 4
                velocity_backward = true;
                velocity_zero = false;
                velocity_forward = false;
                break;
            case 117: // U
                velocity_upward = true;
                velocity_zero = false;
                velocity_forward = false;
                break;
            case 106: // J
                velocity_downward = true;
                velocity_zero = false;
                velocity_upward = false;
                break;
            case 104: // H
                velocity_leftward = true;
                velocity_zero = false;
                velocity_rightward = false;
                break;
            case 107: // K
                velocity_rightward = true;
                velocity_zero = false;
                velocity_leftward = false;
                break;
            case 99: // C
                velocity_circle = true;
                break;
            case 115:    // stop
                {
                //vir1.x = 0;
                    //vir1.y = -0.5;
                vir1.z = 0;
                vir1.roll = 0;


                //vir2.x = 0;
                    //vir2.y = -0.5;
                vir2.z = 0;
                vir2.roll = 0;
                landing = true;
                velocity_circle = false;
                //force_control = false;
                            break;
                }
            case 108:    // close arming
                {
                offb_set_mode.request.custom_mode = "MANUAL";
                set_mode_client.call(offb_set_mode);
                arm_cmd.request.value = false;
                arming_client.call(arm_cmd);

                offb_set_mode2.request.custom_mode = "MANUAL";
                set_mode_client2.call(offb_set_mode2);
                arm_cmd2.request.value = false;
                arming_client2.call(arm_cmd2);
                      break;
                }
            case 63:
                return 0;
                break;
                }
                    }
        /*
        if(velocity_circle){
          vir1.x = 1.6*cos(theta_c);
          vir1.y = 1.6*sin(theta_c);
          theta_c += 0.03;
        }
        */
    if(vir1.roll>pi)
    vir1.roll = vir1.roll - 2*pi;
    else if(vir1.roll<-pi)
    vir1.roll = vir1.roll + 2*pi;

    if(vir2.roll>pi)
    vir2.roll = vir2.roll - 2*pi;
    else if(vir2.roll<-pi)
    vir2.roll = vir2.roll + 2*pi;
        ROS_INFO("setpoint: %.2f, %.2f, %.2f, %.2f", vir1.x, vir1.y, vir1.z, vir1.roll/pi*180);
    follow(vir1,host_mocap,&vs,0,0);
    follow_force(vir2,host_mocap2,&vs2,0,0);

        //mocap_pos_pub.publish(host_mocap);
        //mocap_pos_pub2.publish(host_mocap2);
        local_vel_pub.publish(vs);
        local_vel_pub2.publish(vs2);
        //force_error_pub.publish(force_error);
        //pos_error_pub.publish(pos_error);
        trigger_pub.publish(trigger);
        ros::spinOnce();
        //spinner.stop();
        rate.sleep();
    }

    return 0;
}
