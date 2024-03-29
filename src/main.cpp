#include <uWS/uWS.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "helpers.h"
#include "json.hpp"
#include "spline.h"
#include <math.h>

using nlohmann::json;
using std::string;
using std::vector;

float collision_cost(double car_s, int car_lane, double car_v, int lane, float max_speed, int prev_size, const vector<vector<double>> & sensorFusion) {
  bool obstacle = false;

  bool look_vehicle_ahead = car_lane == lane; 
  bool look_vehicle_next_lane =  abs(car_lane - lane) == 1; 
  if (!look_vehicle_ahead && !look_vehicle_next_lane) {
    return 1.0f;
  }

  for (int i = 0; i<sensorFusion.size(); i++) {
    // car is in my lane
    float d = sensorFusion[i][6];
    int check_car_lane = -1;
    double check_car_s = sensorFusion[i][5];
    double vx = sensorFusion[i][3]; // 'i' car of the road
    double vy = sensorFusion[i][4];
    double check_speed = sqrt(vx*vx+vy*vy); 
    check_car_s += ((double) prev_size *.02*check_speed); 

    if (d>=0 && d<=4){
      check_car_lane = 0;
    } else if (d>=4 && d<=8) {
      check_car_lane = 1;
    } else if (d>=8 && d<=12) {
      check_car_lane = 2;
    }
    
    double safe_distance_rear = car_v > (check_speed+5.0) ? 20 : 40; 
    double safe_distance_front = car_v < (check_speed-5.0) ? 20 : 40;

    if (check_car_lane == lane) {
      if (look_vehicle_ahead) {
        if (check_car_s > car_s && (check_car_s-car_s) < safe_distance_front) {
          return 1.0f;
        }
      } else {
        if (car_s < (check_car_s + safe_distance_front) && car_s > (check_car_s - safe_distance_rear)) {
          return 1.0f;
        }
      }
    }
  }

 return 0.0f;
}


float inefficiency_cost(double car_s, int car_lane, double car_v, int lane, float max_speed, int prev_size, const vector<vector<double>> & sensorFusion) {
	float currentSpeedLane = max_speed;
	for (int i = 0; i<sensorFusion.size(); i++) {
		double vx     = sensorFusion[i][3];
		double vy     = sensorFusion[i][4];
		double check_car_s      = sensorFusion[i][5];
		float check_car_d       = sensorFusion[i][6];
		double check_speed = sqrt(vx*vx+vy*vy);
		check_car_s += ((double) prev_size*.02*check_speed); 

		int car_lane = -1;
		if (check_car_d>0 && check_car_d<4){
	      car_lane = 0;
	    } else if (check_car_d>4 && check_car_d<8) {
	      car_lane = 1;
	    } else if (check_car_d>8 && check_car_d<12) {
	      car_lane = 2;
	    }

	    if (car_lane == lane &&  ((car_s+40.0)>check_car_s) && (car_s<check_car_s) && currentSpeedLane>check_speed ) {
	    	currentSpeedLane = check_speed;
        //printf("speed: %f", check_speed);
	    }
  }
  //printf("current speed lane %d: %f", lane, currentSpeedLane);
	float cost = (2.0*max_speed - currentSpeedLane)/max_speed;
	//printf("EffCost: %f", cost);

  return cost;
}

float diffspeed_cost(double car_s, int car_lane ,double car_v, int lane, float max_speed, int prev_size, const vector<vector<double>> & sensorFusion) {
	float currentSpeedLane = max_speed;
	for (int i = 0; i<sensorFusion.size(); i++) {
		double vx     = sensorFusion[i][3];
		double vy     = sensorFusion[i][4];
		double check_car_s      = sensorFusion[i][5];
		float check_car_d       = sensorFusion[i][6];
		double check_speed = sqrt(vx*vx+vy*vy);
		check_car_s += ((double) prev_size*.02*check_speed); 

		int car_lane = -1;
		if (check_car_d>0 && check_car_d<4){
	      car_lane = 0;
	    } else if (check_car_d>4 && check_car_d<8) {
	      car_lane = 1;
	    } else if (check_car_d>8 && check_car_d<12) {
	      car_lane = 2;
	    }

	    if (car_lane == lane &&  ((car_s+40.0)>check_car_s) && (car_s<check_car_s) && currentSpeedLane>check_speed ) {
	    	currentSpeedLane = check_speed;
        //printf("speed: %f", check_speed);
	    }
  }
  //printf("current speed lane %d: %f", lane, currentSpeedLane);
	float cost = (2.0*car_v - currentSpeedLane)/max_speed;
	//printf("diffCost: %f", cost);
  return cost;
}



float calculate_cost(double car_s, int car_lane, double car_v, int lane, float max_speed, int prev_size, const vector<vector<double>> & sensorFusion) {
	float cost = 0.0;
	vector<std::function<float(double, int, double, int, float, int, const vector<vector<double>> &) >> cf_list = {inefficiency_cost, collision_cost, diffspeed_cost};
	vector<float> weight_list = {0.00/*EFFICIENCY*/, 1.0/*COLLISION*/ , 0.00/*DIFF_SPEED*/};
	for (int i = 0; i < cf_list.size(); ++i) {
	    float new_cost = weight_list[i]*cf_list[i](car_s, car_lane, car_v, lane, max_speed, prev_size, sensorFusion);
	    cost += new_cost;
	  }

	  return cost;
}

std::vector<int> successor_states(int lane, int num_lanes) {
	std::vector<int> lanes;
	if (lane>0) {
		lanes.push_back(lane-1);
	}
  lanes.push_back(lane);
	if (lane<num_lanes-1) {
		lanes.push_back(lane+1);
	}
	return lanes;
}


int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  std::ifstream in_map_(map_file_.c_str(), std::ifstream::in);

  string line;
  while (getline(in_map_, line)) {
    std::istringstream iss(line);
    double x;
    double y;
    float s;
    float d_x;
    float d_y;
    iss >> x;
    iss >> y;
    iss >> s;
    iss >> d_x;
    iss >> d_y;
    map_waypoints_x.push_back(x);
    map_waypoints_y.push_back(y);
    map_waypoints_s.push_back(s);
    map_waypoints_dx.push_back(d_x);
    map_waypoints_dy.push_back(d_y);
  }

  int lane = 1;       
  double ref_vel = 0; 
  double s_change = 0; 

  h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,
               &map_waypoints_dx,&map_waypoints_dy, &lane, &ref_vel, &s_change]
              (uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
               uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
          // Main car's localization Data
          double car_x = j[1]["x"];
          double car_y = j[1]["y"];
          double car_s = j[1]["s"];
          double car_d = j[1]["d"];
          double car_yaw = j[1]["yaw"];
          double car_speed = j[1]["speed"];

          // Previous path data given to the Planner
          auto previous_path_x = j[1]["previous_path_x"];
          auto previous_path_y = j[1]["previous_path_y"];
          // Previous path's end s and d values 
          double end_path_s = j[1]["end_path_s"];
          double end_path_d = j[1]["end_path_d"];

          // Sensor Fusion Data, a list of all other cars on the same side 
          //   of the road.
          auto sensor_fusion = j[1]["sensor_fusion"];

          json msgJson;

          if (previous_path_x.size() > 0) {
            car_s = end_path_s;
          }       
		 int numLanes = 3;
      	 std::vector<int> next_successor_lanes = successor_states(lane, numLanes); // give as a parameter current lanes and the number of lanes of the highway
      	 std::vector<float> costs(numLanes);
         
         for (int iLane = 0; iLane < numLanes; iLane++) {
           costs[iLane] = 1000.0f; // Initializing with Large Cost
         }
         
      	  for (int iLane = 0; iLane<next_successor_lanes.size(); iLane++) {
               double cost_for_state = 0;
               costs[next_successor_lanes[iLane]] = calculate_cost(car_s, lane, car_speed, next_successor_lanes[iLane], 49.5, previous_path_x.size(), sensor_fusion);
           }
           int new_lane = lane;
           float min_cost = costs[new_lane];
         
           for (int iLane = 0; iLane<numLanes; iLane++) {
               float cost = costs[iLane];
               if (cost < min_cost) {
                   min_cost = cost;
                   new_lane = iLane;
               }
           }
		   
		 float newSpeed; 
		// check the speed of the current lane based on other vehicles and speed limit
		for (int i = 0; i<sensor_fusion.size(); i++) {
			double vx     = sensor_fusion[i][3];
			double vy     = sensor_fusion[i][4];
			double check_car_s      = sensor_fusion[i][5];
			float check_car_d       = sensor_fusion[i][6];
			double check_speed = sqrt(vx*vx+vy*vy);
			check_car_s += ((double) previous_path_x.size()*.02*check_speed); 
	
			int car_lane = -1;
			if (check_car_d>0 && check_car_d<4){
			car_lane = 0;
			} else if (check_car_d>4 && check_car_d<8) {
			car_lane = 1;
			} else if (check_car_d>8 && check_car_d<12) {
			car_lane = 2;
			}
	
			if (car_lane == lane &&  ((car_s+40.0)>check_car_s) && (car_s<check_car_s) && check_speed<49.5) {
				newSpeed = check_speed;
				//printf("newSpeed: %f", check_speed);
			}
			else{
				newSpeed =49.5;
			}
		}
          
          if (new_lane != lane && car_s > s_change) {
          	s_change = car_s + 60; 
            lane = new_lane;
          }

          if (newSpeed > ref_vel) {
            ref_vel += 0.224;
          } else {
            if (newSpeed < ref_vel) {
              ref_vel -= 0.224;
            }
          }

          vector<double> ptsx;
          vector<double> ptsy;
          
          double ref_x = car_x;
          double ref_y = car_y;
          double ref_yaw = deg2rad(car_yaw);
		  
          if (previous_path_x.size() < 2) {
            double prev_car_x = car_x - cos(car_yaw);
            double prev_car_y = car_y - sin(car_yaw);
            
            ptsx.push_back(prev_car_x);
            ptsx.push_back(car_x);

            ptsy.push_back(prev_car_y);
            ptsy.push_back(car_y);
            
          } else {
            ref_x = previous_path_x[previous_path_x.size()-1];
            ref_y = previous_path_y[previous_path_x.size()-1];
            
            double ref_x_prev = previous_path_x[previous_path_x.size()-2];
            double ref_y_prev = previous_path_y[previous_path_x.size()-2];
            ref_yaw = atan2(ref_y-ref_y_prev,ref_x-ref_x_prev);
            
            ptsx.push_back(ref_x_prev);
            ptsx.push_back(ref_x);
            
            ptsy.push_back(ref_y_prev);
            ptsy.push_back(ref_y);
          }
          
          // Adding 30m evenly spaced points ahead of the starting reference
          vector<double> next_wp0 = getXY(car_s+30,(2+4*lane),map_waypoints_s,map_waypoints_x,map_waypoints_y);
          vector<double> next_wp1 = getXY(car_s+60,(2+4*lane),map_waypoints_s,map_waypoints_x,map_waypoints_y);
          vector<double> next_wp2 = getXY(car_s+90,(2+4*lane),map_waypoints_s,map_waypoints_x,map_waypoints_y);
          
          ptsx.push_back(next_wp0[0]);
          ptsx.push_back(next_wp1[0]);
          ptsx.push_back(next_wp2[0]);
          
          ptsy.push_back(next_wp0[1]);
          ptsy.push_back(next_wp1[1]);
          ptsy.push_back(next_wp2[1]);
          
         
          for (int i = 0; i< ptsx.size(); i++)
          {
            double shift_x = ptsx[i] - ref_x;
            double shift_y = ptsy[i] - ref_y;
            
            ptsx[i] = shift_x * cos(0-ref_yaw) - shift_y * sin(0-ref_yaw);
            ptsy[i] = shift_x * sin(0-ref_yaw) + shift_y * cos(0-ref_yaw);
          }
          
          // Create a spline
          tk::spline s;
          
          // Set (x,y) points to the spline
          s.set_points(ptsx,ptsy);
          
          // Define the actual (x,y) points we will use for the planner
          vector<double> next_x_vals;
          vector<double> next_y_vals;
          
          // Start with all of the previous path points from last time
          for (int i = 0; i < previous_path_x.size(); i++)
          {
            next_x_vals.push_back(previous_path_x[i]);
            next_y_vals.push_back(previous_path_y[i]);
          }
          
          // Calculate how to break up spline points, so that we travel at our desired reference velocity
          double target_x = 30.0;
          double target_y = s(target_x);
          double target_dist = sqrt((target_x*target_x)+(target_y*target_y));
          
          double x_add_on = 0;
                    
          // Fill up the rest of the path planner after filling the previous points, here we always output 50 points
          for( int i = 1; i < 50 - previous_path_x.size(); i++ ) {
            // Time interval between waypoints in seconds
            const double time_wp_update = 0.02;
            const double five_mph_in_mps = 5 * 0.44704; // 2.24 [m/s]
            
            double N = target_dist / (time_wp_update * ref_vel/five_mph_in_mps);
            double x_point = x_add_on + target_x/N;
            double y_point = s(x_point);
            
            x_add_on = x_point;
            
            double x_ref = x_point;
            double y_ref = y_point;
            
            // Rotate the local coordinate back to the global coordinate
            x_point = x_ref * cos(ref_yaw) - y_ref * sin(ref_yaw);
            y_point = x_ref * sin(ref_yaw) + y_ref * cos(ref_yaw);
            
            x_point += ref_x;
            y_point += ref_y;
            
            next_x_vals.push_back(x_point);
            next_y_vals.push_back(y_point);
          }
          


          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;

          auto msg = "42[\"control\","+ msgJson.dump()+"]";

          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }  // end "telemetry" if
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }  // end websocket if
  }); // end h.onMessage

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  
  h.run();
}