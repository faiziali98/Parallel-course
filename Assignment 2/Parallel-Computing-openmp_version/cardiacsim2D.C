/* 
 * Solves the Panfilov model using an explicit numerical scheme.
 * Based on code orginally provided by Xing Cai, Simula Research Laboratory 
 * and reimplementation by Scott B. Baden, UCSD
 * 
 * Modified and  restructured by Didem Unat, Koc University
 *
 */
#include <mpi.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <iostream>
#include <iomanip>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <omp.h>
using namespace std;

// Utilities
//

// Timer
// Make successive calls and take a difference to get the elapsed time.
static const double kMicro = 1.0e-6;
double getTime()
{
	struct timeval TV;
	struct timezone TZ;

	const int RC = gettimeofday(&TV, &TZ);
	if (RC == -1)
	{
		cerr << "ERROR: Bad call to gettimeofday" << endl;
		return (-1);
	}

	return (((double)TV.tv_sec) + kMicro * ((double)TV.tv_usec));

} // end getTime()

// Allocate a 2D array
double **alloc2D(int m, int n)
{
	double **E;
	int nx = n, ny = m;
	E = (double **)malloc(sizeof(double *) * ny + sizeof(double) * nx * ny);
	assert(E);
	int j;
	for (j = 0; j < ny; j++)
		E[j] = (double *)(E + ny) + j * nx;
	return (E);
}

// Reports statistics about the computation
// These values should not vary (except to within roundoff)
// when we use different numbers of  processes to solve the problem
double stats(double **E, int m, int n, double *_mx)
{
	double mx = -1;
	double l2norm = 0;
	int i, j;
	for (j = 1; j <= m; j++)
		for (i = 1; i <= n; i++)
		{
			l2norm += E[j][i] * E[j][i];
			if (E[j][i] > mx)
				mx = E[j][i];
		}
	*_mx = mx;
	// l2norm /= (double)((m) * (n));
	// l2norm = sqrt(l2norm);
	return l2norm;
}

// External functions
extern "C"
{
	void splot(double **E, double T, int niter, int m, int n);
}
void cmdLine(int argc, char *argv[], double &T, int &n, int &px, int &py, int &plot_freq, int &no_comm, int &num_threads);

void simulate(double **E, double **E_prev, double **R,
			  const double alpha, const int n, const int m, const double kk,
			  const double dt, const double a, const double epsilon,
			  const double M1, const double M2, const double b, const int my_rank,
			  const int px, const int py, const int mr, const int mc, const int ncomm, const int num_of_threads)
{
	int i, j;
	double *tSendl, *tRecvl, *tSendr, *tRecvr;

	int tj = my_rank / px;
	int ti = my_rank % px;


	MPI_Request recv_request[4];
	MPI_Request send_request[4];
	MPI_Status send_status[4];
	MPI_Status recv_status[4];

	/* 
	* Copy data from boundary of the computational box 
	* to the padding region, set up for differencing
	* on the boundary of the computational box
	* Using mirror boundaries
	*/

	// For Top row
	if (ncomm == 0){
		if (tj > 0)
		{
			int dest_proc = my_rank - px;
			// cout<< "Here 0 " << dest_proc <<endl;
			int src_proc = dest_proc;

			MPI_Isend(&(E_prev[1][1]), n, MPI_DOUBLE, dest_proc,
					1, MPI_COMM_WORLD, &send_request[0]);
			MPI_Irecv(&(E_prev[0][1]), n, MPI_DOUBLE, src_proc,
					2, MPI_COMM_WORLD, &recv_request[0]);
		}

		//For Bottom row
		if (tj < py - 1)
		{
			int dest_proc = my_rank + px;
			int src_proc = dest_proc;
			// cout << "Here 1 " << dest_proc << endl;

			MPI_Isend(&(E_prev[m][1]), n, MPI_DOUBLE, dest_proc,
					2, MPI_COMM_WORLD, &send_request[1]);
			MPI_Irecv(&(E_prev[m + 1][1]), n, MPI_DOUBLE, src_proc,
					1, MPI_COMM_WORLD, &recv_request[1]);
		}

		//For left coloumn
		if (ti > 0)
		{
			int dest_proc = my_rank - 1;
			int src_proc = dest_proc;
			// cout << "Here 2 " << dest_proc << endl;

			tSendl = (double *)(malloc(sizeof(double) * m));
			tRecvl = (double *)(malloc(sizeof(double) * m));

			// cout << sizeof(E_prev) << endl;
			for (j = 1; j <= m; j++)
			{
				tSendl[j - 1] = E_prev[j][1];
			}

			MPI_Isend(tSendl, m, MPI_DOUBLE, dest_proc,
					3, MPI_COMM_WORLD, &send_request[2]);
			MPI_Irecv(tRecvl, m, MPI_DOUBLE, src_proc,
					4, MPI_COMM_WORLD, &recv_request[2]);
		}

		//For Right coloumn
		if (ti < px - 1)
		{
			int dest_proc = my_rank + 1;
			int src_proc = dest_proc;
			// cout << "Here 3 " << dest_proc << endl;

			tSendr = (double *)(malloc(sizeof(double) * m));
			tRecvr = (double *)(malloc(sizeof(double) * m));

			for (j = 1; j <= m; j++)
			{
				tSendr[j - 1] = E_prev[j][n];
			}

			MPI_Isend(tSendr, m, MPI_DOUBLE, dest_proc,
					4, MPI_COMM_WORLD, &send_request[3]);
			MPI_Irecv(tRecvr, m, MPI_DOUBLE, src_proc,
					3, MPI_COMM_WORLD, &recv_request[3]);
		}

		if (tj > 0)
		{
			MPI_Wait(&(recv_request[0]), &(recv_status[0]));
			MPI_Wait(&(send_request[0]), &(send_status[0]));
		}
		if (tj < py - 1)
		{
			MPI_Wait(&(recv_request[1]), &(recv_status[1]));
			MPI_Wait(&(send_request[1]), &(send_status[1]));
		}

		if (ti > 0)
		{
			MPI_Wait(&(recv_request[2]), &(recv_status[2]));
			MPI_Wait(&(send_request[2]), &(send_status[2]));
		}
		if (ti < px - 1)
		{
			MPI_Wait(&(recv_request[3]), &(recv_status[3]));
			MPI_Wait(&(send_request[3]), &(send_status[3]));
		}
	}

	#pragma omp parallel shared(E_prev, tj, ti, i, j) num_threads(num_of_threads)
	{
		#pragma omp single nowait
		{

			if (tj == 0)
			{
				#pragma omp task shared(E_prev) firstprivate(i)
				{
					for (i = 1; i <= n; i++)
						E_prev[0][i] = E_prev[2][i];
				}
			}
			
			if (tj == py - 1)
			{
				#pragma omp task shared(E_prev) firstprivate(i)
				{
					for (i = 1; i <= n; i++)
					E_prev[m + 1][i] = E_prev[m - 1][i];
				}
			}

			if (ti == 0)
			{
				#pragma omp task shared(E_prev) firstprivate(j)
				{
					for (j = 1; j <= m; j++)
						E_prev[j][0] = E_prev[j][2];
				}
			}
			
			if (ti == px - 1)
			{
				#pragma omp task shared(E_prev) firstprivate(j)
				{
					for (j = 1; j <= m; j++)
						E_prev[j][n + 1] = E_prev[j][n - 1];
				}
			}

			if (ncomm == 0)
			{
				if (ti > 0)
				{
					#pragma omp task shared(E_prev) firstprivate(j)
					{
						for (j = 1; j <= m; j++)
						{
							E_prev[j][0] = tRecvl[j - 1];
						}
					}
				}
				if (ti < px - 1)
				{
					#pragma omp task shared(E_prev) firstprivate(j)
					{
						for (j = 1; j <= m; j++)
						{
							E_prev[j][n + 1] = tRecvr[j - 1];
						}
					}
				}
			}	
		}
		#pragma omp taskwait
	}


	// Solve for the excitation, the PDE
	#pragma omp parallel for collapse(2) private(i,j) num_threads(num_of_threads)
	for (j = 1; j <= m; j++)
	{
		for (i = 1; i <= n; i++)
		{
			E[j][i] = E_prev[j][i] + alpha * (E_prev[j][i + 1] + E_prev[j][i - 1] - 4 * E_prev[j][i] + E_prev[j + 1][i] + E_prev[j - 1][i]);
		}
	}

	// // // /*
	// // // * Solve the ODE, advancing excitation and recovery to the
	// // // *     next timtestep
	// // // */

	#pragma omp parallel for collapse(2) private(i,j) num_threads(num_of_threads)
	for (j = 1; j <= m; j++)
	{
		
		for (i = 1; i <= n; i++)
		{   
			int jIndc = j + (mr * tj);
			int iIndc = i + (mc * ti);
			E[j][i] = E[j][i] - dt * (kk * E[j][i] * (E[j][i] - a) * (E[j][i] - 1) + E[j][i] * R[jIndc][iIndc]);
		}
	}

	#pragma omp parallel for collapse(2) private(i,j) num_threads(num_of_threads)
	for (j = 1; j <= m; j++)
	{
		
		for (i = 1; i <= n; i++)
		{
			int jIndc = j + (mr * tj);
			int iIndc = i + (mc * ti);
			R[jIndc][iIndc] = R[jIndc][iIndc] + dt * (epsilon + M1 * R[jIndc][iIndc] / (E[j][i] + M2)) * (-R[jIndc][iIndc] - kk * E[j][i] * (E[j][i] - b - 1));
		}
	}
}

// Main program
int main(int argc, char **argv)
{

	int my_rank, world_size;

	MPI_Init(NULL, NULL);
	MPI_Comm_size(MPI_COMM_WORLD, &world_size);
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
	/*
	*  Solution arrays
	*   E is the "Excitation" variable, a voltage
	*   R is the "Recovery" variable
	*   E_prev is the Excitation variable for the previous timestep,
	*      and is used in time integration
	*/
	double **E, **R, **E_prev;

	// Various constants - these definitions shouldn't change
	const double a = 0.1, b = 0.1, kk = 8.0, M1 = 0.07, M2 = 0.3, epsilon = 0.01, d = 5e-5;

	double T = 1000.0;
	int m = 200, n = 200;
	int plot_freq = 0;
	int px = 1, py = 1;
	int no_comm = 0;
	int num_of_threads = 1;

	cmdLine(argc, argv, T, n, px, py, plot_freq, no_comm, num_of_threads);

	if (world_size != (px*py)){
		return 0;
	}

	m = n;
	// Allocate contiguous memory for solution arrays
	// The computational box is defined on [1:m+1,1:n+1]
	// We pad the arrays in order to facilitate differencing on the
	// boundaries of the computation box
	E_prev = alloc2D(m + 2, n + 2);
	R = alloc2D(m + 2, n + 2);
	E = alloc2D(m + 2, n + 2);

	int i, j;
	// Initialization
	for (j = 1; j <= m; j++)
		for (i = 1; i <= n; i++)
			E_prev[j][i] = R[j][i] = 0;

	for (j = 1; j <= m; j++)
		for (i = n / 2 + 1; i <= n; i++)
			E_prev[j][i] = 1.0;

	for (j = m / 2 + 1; j <= m; j++)
		for (i = 1; i <= n; i++)
			R[j][i] = 1.0;

	double dx = 1.0 / n;

	// For time integration, these values shouldn't change
	double rp = kk * (b + 1) * (b + 1) / 4;
	double dte = (dx * dx) / (d * 4 + ((dx * dx)) * (rp + kk));
	double dtr = 1 / (epsilon + ((M1 / M2) * rp));
	double dt = (dte < dtr) ? 0.95 * dte : 0.95 * dtr;
	double alpha = d * dt / (dx * dx);

	double **myE, **myEprev;

	int tj = my_rank / px;
	int ti = my_rank % px;

	int ndr, ndc, my_rows, my_cols, multr, multc;
	// int sqroot = sqrt(world_size);

	if ((m % py) != 0){
		
		ndr = m;
		while (ndr % py != 0)
			ndr ++;

		multr = (ndr / py);
		if (tj == (py - 1)){
			my_rows = m - (multr * (py - 1));
		}else{
			my_rows = multr;
		}
		
	} else {
		my_rows = m / py;
		multr = my_rows;
	}

	if ((n % px) != 0)
	{

		ndc = n;
		while (ndc % px != 0)
			ndc++;

		multc = (ndc / px);
		if (ti == (px - 1)){
			my_cols = n - (multc * (px - 1));
		}
		else{
			my_cols = multc;
		}
	}
	else
	{
		my_cols = n / px;
		multc = my_cols;
	}

	myEprev = alloc2D(my_rows + 2, my_cols + 2);
	myE = alloc2D(my_rows + 2, my_cols + 2);

	for (j = 1; j <= my_rows; j++)
	{
		for (i = 1; i <= my_cols; i++)
		{
			myEprev[j][i] = E_prev[j + (multr * tj)][i + (multc * ti)];
		}
	}

	int sizes[2] = {m, n};
	int subsizes[2] = {my_rows, my_cols};
	int start[2] = {0, 0};

	MPI_Datatype type, subarray;
	MPI_Type_create_subarray(2, sizes, subsizes, start, MPI_ORDER_C, MPI_DOUBLE, &type);
	MPI_Type_create_resized(type, 0, (my_cols) * sizeof(double), &subarray);
	MPI_Type_commit(&subarray);

	int sendcounts[world_size];
	int displs[world_size];

	if (my_rank == 0)
	{
		for (i = 0; i < world_size; i++)
			sendcounts[i] = 1;

		int disp = 0;
		for (i = 0; i < py; i++)
		{
			for (j = 0; j < px; j++)
			{
				displs[i * (px) + j] = disp;
				disp += 1;
			}
			if (i != py-1){
				disp += (((m / (py)) - 1) * (px)) + (m % py);
			}else{
				disp += (((m / (py)) - 1) * (px)) + (m % py);
			}
		}

		cout << "Grid Size       : " << n << endl;
		cout << "Duration of Sim : " << T << endl;
		cout << "Time step dt    : " << dt << endl;
		cout << "Process geometry: " << px << " x " << py << endl;

		if (no_comm)
			cout << "Communication   : DISABLED" << endl;
		cout << endl;
	}

	// Simulated time is different from the integer timestep number
	// Simulated time
	double t = 0.0;
	// Integer timestep number
	int niter = 0;

	double **tmp1, **globaltmp;
	tmp1 = alloc2D(my_rows, my_cols);
	globaltmp = alloc2D(m, n);

	// Start the timer
	double t0 = getTime();

	while (t < T)
	{
		t += dt;
		niter++;

		simulate(myE, myEprev, R, alpha, my_cols, my_rows, kk, dt, a, epsilon, 
				M1, M2, b, my_rank, px, py, multr, multc, no_comm, num_of_threads);

		//swap current E with previous E
		double **tmp = myE;
		myE = myEprev;
		myEprev = tmp;

		if (plot_freq)
		{
			int k = (int)(t / plot_freq);
			if ((t - k * plot_freq) < dt)
			{
				for (j = 1; j <= my_rows; j++){
					for (i = 1; i <= my_cols; i++){
						tmp1[j-1][i-1] = myE[j][i];
					}
				}

				MPI_Gatherv(&(tmp1[0][0]), my_rows * my_cols, MPI_DOUBLE, &(globaltmp[0][0]), sendcounts, displs, subarray, 0, MPI_COMM_WORLD);
				
				// MPI_Barrier(MPI_COMM_WORLD);
				if (my_rank == 0)
				{
					splot(globaltmp, t, niter, m, n);
				}
			}
		}
	} //end of while loop

	MPI_Barrier(MPI_COMM_WORLD);
	double time_elapsed = getTime() - t0;

	double mx;
	double gmax, glnorm;
	double l2norm = stats(myEprev, my_rows, my_cols, &mx);

	MPI_Reduce(&mx, &gmax, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
	MPI_Reduce(&l2norm, &glnorm, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

	if (my_rank == 0)
	{
		double Gflops = (double)(niter * (1E-9 * n * n) * 28.0) / time_elapsed;
		double BW = (double)(niter * 1E-9 * (n * n * sizeof(double) * 4.0)) / time_elapsed;

		cout << "Number of Iterations        : " << niter << endl;
		cout << "Elapsed Time (sec)          : " << time_elapsed << endl;
		cout << "Sustained Gflops Rate       : " << Gflops << endl;
		cout << "Sustained Bandwidth (GB/sec): " << BW << endl
			 << endl;

		glnorm = sqrt(glnorm / (double)(m * n));
		cout << "Max: " << gmax << " L2norm: " << glnorm << endl;
	}

	MPI_Barrier(MPI_COMM_WORLD);

	free(E);
	free(E_prev);
	free(R);
	free(myEprev);
	free(myE);
	free(tmp1);
	free(globaltmp);

	MPI_Finalize();
	return 0;
}