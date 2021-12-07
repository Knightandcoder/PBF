#include <Fluid.h>
#include <kernel.h>
#include <vorticity.h>
#include <viscocity.h>

#include <iostream>
#include <chrono>
typedef std::chrono::high_resolution_clock Clock;

#define DEBUG 1

Fluid::Fluid(int num_particles, double particle_mass, double rho, double gravity_f, double user_f, int jacobi_iterations, 
			double cfm_epsilon, double kernel_h, double tensile_k, double tensile_delta_q, int tensile_n, 
			double viscocity_c, double vorticity_epsilon, double lower_bound, double upper_bound, double dt){

        this->num_particles = num_particles;
	this->particle_mass = particle_mass;
	this->rho = rho;

	this->user_f = user_f;

	this->jacobi_iterations = jacobi_iterations;

	this->cfm_epsilon = cfm_epsilon;
	this->kernel_h = kernel_h;

	this->tensile_k = tensile_k;
	this->tensile_delta_q = tensile_delta_q;
	this->tensile_n = tensile_n;

	this->viscocity_c = viscocity_c;
	this->vorticity_epsilon = vorticity_epsilon;

	this->t = 0;
	this->dt = dt;

	this->lower_bound = lower_bound;
	this->upper_bound = upper_bound;

        this->grid = SpatialHashGrid(lower_bound, upper_bound, kernel_h);

        this->x_new.resize(num_particles, 3);
        this->v.resize(num_particles, 3);
        this->dP.resize(num_particles, 3);
        this->omega.resize(num_particles, 3);
        this->eta.resize(num_particles, 3);
        this->N.resize(num_particles, 3);
        this->vorticity_f.resize(num_particles, 3);
        this->cell_coord.resize(num_particles, 3);

        this->density.resize(num_particles);
        this->c.resize(num_particles);
        this->lambda.resize(num_particles);
        this->c_grad_norm.resize(num_particles);

        this->gravity_f = Eigen::VectorXd::Constant(num_particles, gravity_f);

        std::vector<std::vector<int>> neighbours_init(num_particles);
        this->neighbours = neighbours_init;

        this->tensile_stability_denom = pow(kernel_poly6(tensile_delta_q, kernel_h), tensile_n);

}	

void Fluid::init_state(Eigen::MatrixXd &fluid_state){
        v.setZero();
        grid.update(fluid_state);
}

void Fluid::step(Eigen::MatrixXd &fluid_state, Eigen::MatrixXd &colors){
        auto t0 = Clock::now();

	// Apply External forces
        v.col(1) -= particle_mass * gravity_f;
        x_new = fluid_state + dt * v;

        auto t1 = Clock::now();
        if (DEBUG) std::cout << "\n------------------------------------------\nApplied Externel forces [" << std::chrono::duration_cast<std::chrono::seconds>(t1 - t0).count() << " s]\n";

        // Get neighbours using spatial hash grid
        grid.findNeighbours(x_new, neighbours);

        auto t2 = Clock::now();
        if (DEBUG) std::cout << "Found Neighbours [" << std::chrono::duration_cast<std::chrono::seconds>(t2 - t1).count() << " s]\n";

	//Jacobi Loop 
	for(int i = 0; i < jacobi_iterations; i++){
                // reset jacobi state
                density.setZero();
                lambda.setZero();
                c_grad_norm.setZero();

                for(int p_i = 0; p_i < num_particles; p_i++){
                        
                        // compute densities
                        for(int p_j : neighbours[i]){
                                density[p_i] += particle_mass * kernel_poly6(x_new.row(p_i), x_new.row(p_j), kernel_h);

                                // accumulate gradient norm
                                c_grad_temp.setZero();
                                if (p_i == p_j){
                                        for(int p_k : neighbours[p_i]){
                                               kernel_spiky(ker_res, x_new.row(p_i), x_new.row(p_k), kernel_h);
                                               c_grad_temp += ker_res;
                                        }
                                }
                                else{
                                        kernel_spiky(ker_res, x_new.row(p_i), x_new.row(p_j), kernel_h);
                                        c_grad_temp = ker_res;
                                }

                                c_grad_norm[i] += (c_grad_temp / rho).norm();
                        }

                        // Compute constraint and lambda
                        c[p_i] = (density[p_i] / rho) - 1;
                        lambda[p_i] = -c[p_i] / (c_grad_norm[p_i] + cfm_epsilon);
                }

                auto t3 = Clock::now();
                if (DEBUG) std::cout << "Computed Constraints [" << std::chrono::duration_cast<std::chrono::seconds>(t3 - t2).count() << " s]\n";

		// Compute dP
                dP.setZero();
                for(int p_i = 0; p_i < num_particles; p_i++){
                        for(int p_j : neighbours[i]){
                                kernel_spiky(ker_res, x_new.row(p_i), x_new.row(p_j), kernel_h);
		                s_corr = - tensile_k * pow(kernel_poly6(x_new.row(p_i), x_new.row(p_j), kernel_h), tensile_n) / tensile_stability_denom;
                                dP.row(p_i) += (lambda[p_i] + lambda[p_j] + s_corr) * ker_res; 
                        }
                        dP.row(p_i) /= rho;
                }

                auto t4 = Clock::now();
                if (DEBUG) std::cout << "Computed Position Correction [" << std::chrono::duration_cast<std::chrono::seconds>(t4 - t3).count() << " s]\n";

		// Collision Detection with boundary box and solid
                for(int p_i = 0; p_i < num_particles; p_i++){
			x_new.row(p_i) += 0.005 * dP.row(p_i); // TODO CALIBRATE SIMULATION
                        
			//Collision Detect correct p.x_new (naive)
                        for(int axis = 0; axis < 3; axis++){
                                if (x_new.row(p_i)[axis] < lower_bound){ 
                                        x_new.row(p_i)[axis] = lower_bound;
                                        if (v(p_i, axis) < 0) v(p_i, axis) *= -1;
                                }
                                if (x_new.row(p_i)[axis] > upper_bound){ 
                                        x_new.row(p_i)[axis] = upper_bound;
                                        if (v(p_i, axis) > 0) v(p_i, axis) *= -1;
                                }
                        }
                }
                auto t5 = Clock::now();
                if (DEBUG) std::cout << "Collision Detection [" << std::chrono::duration_cast<std::chrono::seconds>(t5 - t4).count() << " s]\n";
	}

	//Update Velocity
        v = (x_new - fluid_state) / dt;

	// Vorticity (O(n^2))
	// apply_vorticity(fluid, kernel_h, vorticity_epsilon, dt);
	// apply_viscocity(fluid, kernel_h, viscocity_c);


	// Update Position and spatial hash grid
        fluid_state = x_new;
        grid.update(fluid_state);


        auto t6 = Clock::now();
        if (DEBUG) std::cout << "Simulation Step Total Time [" << std::chrono::duration_cast<std::chrono::seconds>(t6 - t0).count() << " s]\n----------------------------------------\n";

        // Debugging using colors for now.
        colors.row(0) << 1, 0, 0; // track particle 0 in red
        for (int i = 1; i < num_particles; i++){
                colors.row(i) << 0, 0, 1; 
        }
        for (auto n_idx : neighbours[0]){
                if (n_idx != 0){
                        colors.row(n_idx) << 0, 1, 0; // track neighbours in green
                }
        }

	t += dt;
}
