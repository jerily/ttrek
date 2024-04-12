/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <string.h>
#include "subCmdDecls.h"
#include "minisat/Solver.h"
#include "resolvo/tests/solver.h"
#include <signal.h>

using namespace Minisat;

static Solver* solver;
// Terminate by notifying the solver and back out gracefully. This is mainly to have a test-case
// for this feature of the Solver as it may take longer than an immediate call to '_exit()'.
static void SIGINT_interrupt(int signum) { solver->interrupt(); }

// Note that '_exit()' rather than 'exit()' has to be used. The reason is that 'exit()' calls
// destructors and may cause deadlocks if a malloc/free function happens to be running (these
// functions are guarded by locks for multithreaded use).
static void SIGINT_exit(int signum) {
    printf("\n"); printf("*** INTERRUPTED ***\n");
    if (solver->verbosity > 0){
//        printStats(*solver);
        printf("\n"); printf("*** INTERRUPTED ***\n"); }
    _exit(1); }

int ttrek_PretendSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    BundleBoxProvider provider;
    provider.requirements({"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"});


    Solver S;
    solver = &S;
    // Use signal handlers that forcibly quit until the solver will be able to respond to
    // interrupts:
    signal(SIGINT, SIGINT_exit);
    signal(SIGXCPU,SIGINT_exit);

    // Change to signal-handlers that will only notify the solver and allow it to terminate
    // voluntarily:
    signal(SIGINT, SIGINT_interrupt);
    signal(SIGXCPU,SIGINT_interrupt);

    if (!S.simplify()) {
        return TCL_OK;
    }
    Var v0 = S.newVar();
    Var v1 = S.newVar();
    Var v2 = S.newVar();
    vec<Lit> lits;
    lits.push(mkLit(v0, true));
    lits.push(mkLit(v1, false));
    lits.push(mkLit(v2, true));
    S.addClause(lits);
    S.budgetOff();
    vec<Lit> dummy;
//    dummy.push(mkLit(v0,false));
    S.verbosity = 1;
    bool ret = S.solve(dummy);
    if (ret) {
        S.toDimacs(stdout, dummy);
        fprintf(stderr, "SAT\n");
        for (int i = 0; i < S.nVars(); i++)
            if (S.model[i] != l_Undef)
                fprintf(stdout, "%s%s%d", (i==0)?"":" ", (S.model[i]==l_True)?"":"-", i+1);
        fprintf(stdout, "\n");
        SetResult("SAT");
    } else {
        fprintf(stderr, "UNSAT\n");
        SetResult("UNSAT");
    }
    return TCL_OK;
}
