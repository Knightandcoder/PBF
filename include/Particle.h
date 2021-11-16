#ifndef PARTICLE
#define PARTICLE

#include <Eigen/Dense>
#include <list>

class Particle{
public:
	// Basic State Parameters
	double m; // particle mass (assumed identically 1 across all particles)
	double rho = 0;
	Eigen::Vector3d x; // World Space Position
	Eigen::Vector3d X; // Undeformed Position
	Eigen::Vector3d v; // Velocity
	Eigen::Vector3d f; // Forces

	// Neighourhood Parameters
	std::list<Particle> neighbors; // list of particles which neighour the current particle

	// Jacobi Parameters
	double c; // density constraint result
	double lambda; // magnitude of force solving constraint gradient
	double c_grad_neighorhood_norm; // 2-norm of constraint gradient accumulated over neighoring particles
	Eigen::Vector3d dP; // total position update including corrections from neighbor particle density constraints
	Eigen::Vector3d dX; // change in position 

	Eigen::Vector3d x_new; // updated position
	Eigen::Vector3d v_new; // updated velocity

	// Vorticity Parameters
	Eigen::Vector3d omega; // the curl at the particles position
	Eigen::Vector3d eta; // differential operator acting on omega
	Eigen::Vector3d N; // Normalized location vector
	Eigen::Vector3d vorticity_f; // corrective vorticity force


public:
	/*
	Initialize a new fluid particle at position X_init and with mass m.
	*/
	Particle(Eigen::Vector3d X_init, double m);
};

#endif