%/////////////////////////////////////////////////
% Research MATLAB code : Parameters required for simulation
% Author: Janaka (856518198)
% Date  : 08/31/2023
%
% Description:
%   This MATLAB script generate a structure with the parameters need in
%   various places in the simulation. All the parameters are in SI units
%
% Inputs:
%
% Outputs:
%   param       : structure of parameters
%/////////////////////////////////////////////////
function param = parametersGen
dataDir = fileparts(mfilename('fullpath'));
cmpx = load(fullfile(dataDir, 'complex_model_dat.mat'));
dipole = load(fullfile(dataDir, 'dipole_model_dat.mat'));

%//////////////////////////// Universal Constant /////////////////////////
param.g = 9.81;                     % gravitational acceleration
param.mu0 = 4*pi*1e-7;              % free space permiability
param.khi = 1000;                   % khi value of free space

%//////////////////////////// Unit Convertion ////////////////////////////
param.mm2m = 1e-3;                  % Convert millimeters to meters
param.Kg2g = 1e-3;                  % Convert Kilograms to grams
param.rad2deg = 180/pi;             % Convert radian to degrees
param.deg2rad = pi/180;             % Convert degree to radian

%//////////////////////// Apparatus dimentions ///////////////////////////
param.a= param.mm2m*34.3;                               % Distance form center (global zero) to the magnet face
param.b= param.mm2m*(19.5/2);                           % Magnet radius,
param.c= param.mm2m*(35/2);                             % Petridish radius
param.ap = (39.5 / 2) * param.mm2m;
param.aw = 30 * param.mm2m;
param.n=6;                                              % Number of Magnets
param.phi_deg = (2*180/param.n).*(0 : (param.n-1))';        % magnets angles w.r.t global coordinate system in degrees 
param.phi_rad = (2*pi/param.n).*(0 : (param.n-1))';         % magnets angles w.r.t global coordinate system in radians 
param.rho_k_deg = (param.b + param.a).*[cosd(param.phi_deg) sind(param.phi_deg)];     % magnets position vector w.r.t global coordinate system (angles in degrees) 
param.rho_k_rad = (param.b + param.a).*[cos(param.phi_rad) sin(param.phi_rad)];       % magnets position vector w.r.t global coordinate system (angles in radian)

%//////////////////////// Actuator parameters ////////////////////////////
param.omega_n = 75;                 % Cutoff frequency of the actuator
param.zeta = 0.75;                  % Damping ration

%/////////////////////// Feromagnetic bead ///////////////////////////////
param.pr = param.mm2m*(3/2);        % radius of the feromagnetic particle
param.ir_density = 7800;            % Density of iron
param.pmass = param.ir_density*((4/3)*pi*(param.pr)^3); % mass of the feromagnetic particle

%////////////////////////// Viscuss medium ///////////////////////////////
param.Vss = 1.3*1e-2;
%param.Vss = 0.5*1e-2;
%param.Vss = 5*1e-2;                % terminal velocity of the ferromagnetic particle in viscuss fluid
%param.Vss = 10*1e-2;                % terminal velocity of the ferromagnetic particle in viscuss fluid
%param.Vss = 1*1e-2;
param.rho_l = 1400;                 % density of the viscus medium
param.sigma_v=(param.g/(param.Vss))*(1 - (param.rho_l/param.ir_density));                 % related to viscosity of the fluid in the petridish and the diameter of magnetic bead --> mu/m (similery g/Vss) 

%////////////////////////// Force constant ///////////////////////////////
param.km = (param.khi*param.pmass/(2*param.mu0*param.ir_density*(1+(param.khi/3))));      % force constan (km) the feromagnetic particle
param.kg = (param.khi/(2*param.mu0*param.ir_density*(1+(param.khi/3))));                  % force constant (kg = km/m) per unit mass

%////////////////////////// Magnetic model /////////////////////////////// 
param.delta_r = param.mm2m*0.5;    % delta r value used to generate linearized matrices
param.delta_th = param.deg2rad*5;  % delta theta value used to generate linearied matrices
param.m = [1 0 0]';                % Magnetisation vector of the dipole model
param.rho = cmpx.rho;              % Position vectors of complex magnetic model
param.ak_comp = cmpx.ak;           % Coefficient of positions
param.ak_dipole = dipole.ak;       % Dipole model k value

%////////////////////////////// PID parameters ////////////////////////////
param.Kp = 850;                    % propotional gain
%param.Ki = 2;                     % integral gain gain

%///////////////////// minimizes inf norm controller //////////////////////
param.crit_rad = param.mm2m*0.001; % critical radius

%///////////////////////// Simulator configurations ///////////////////////
%% Configurable variables
param.t_step_count = 1;

%% Simulation Parameters
param.motorson = 1;                 % enable motor dynemics
param.model_sel = 0;               % Select the magnet model (0 = complex model, 1 = dipole model(TODO))
param.ctrl_select = 4; % Control selection (0: Open-loop, 1: 2-norm minimization control, 2: Approximate feedback linearization, 3: Feedback linearization, 4: Robust Control, 5: Ronust Homotopy)
param.cntrl_type = 1;  % Control type (0: Regulatory, 1: Tracking)
param.rd_vel = 2 * param.mm2m;  % Velocity of the reference trajectory (mm/s)
param.rd_scale = 10;            % Scaling factor for the reference trajectory (factor 2.4 good for sipiral traj)
param.rd_rot_deg = 0;           % Rotation of the referece trajectory
param.rd_select = 7;            % Reference selection (-3: Half square 2 trajectory, -2: Half square trajectory, -1: Single point trajectory, 0: Simple square, 1: Complex square, 2: Spiral, 3: SIU trajectory, 4: Line, 5: Circle, 6: Steps, 7: Steps x and y)

param.rd_1_select = param.rd_select;
param.rd_2_select = 1;

param.tss = 0;                 % Simulation start time (s) [use only integer values]
param.tfs = 190;                % Simulation end time (s) [use only integer values]
param.tnps = 40;               % Number of points in the simulation

param.beta = 0.1;              % pealize ||u|| factor

%% Lead Cotrol pole and zero
param.leadz = param.sigma_v;
param.leadp = 300;


%% Step jump referece trajectory parameters (law viscosity)
% param.id_square_centered = true;   % unit square centered at origin
% param.id_max_jump = 0.15;          % maximum random jump distance
% param.id_min_jump = 0.08;          % optional minimum jump distance
% param.id_min_dwell = 0.1;         % shortest holding time
% param.id_max_dwell = 1.5;         % longest holding time
% param.scale_factor = 20e-3;

%% Step jump referece trajectory parameters (high Viscocity)
param.id_square_centered = true;   % unit square centered at origin
param.id_max_jump = 0.25;          % maximum random jump distance
%param.id_max_jump = 0.35;          % maximum random jump distance
param.id_min_jump = 0.1;          % optional minimum jump distance
param.id_min_dwell = 0.4;         % shortest holding time
param.id_max_dwell = 3;         % longest holding time
param.scale_factor = 20e-3;

param.id_log_uniform_dwell = true; % gives both short and long dwell times
param.id_seed = 10;                % reproducible random trajectory
%% Pulse referece trajectory parameters
param.pulse_amplitude1 = 0.2;
param.pulse_amplitude2 = 0.2;
param.pulse1_start    = 1;    % First pulse starts at 2 s
param.pulse1_duration = 5;    % First pulse lasts 5 s
param.pulse2_delay    = 5;   % Delay after first pulse ends
%param.pulse2_start    = 1;
param.pulse2_duration = 5;    % Second pulse lasts 5 s

param.pulse1_direction = 1;   % 1 = x direction
param.pulse2_direction = 2;   % 2 = y direction
