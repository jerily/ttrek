/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <git2.h>
#include "ttrek_git.h"

int ttrek_GitInit(ttrek_state_t *state_ptr) {
    // initialize the libgit2 library
    git_libgit2_init();

    git_repository *repo = NULL;
    git_index *index = NULL;
    git_oid tree_oid;
    git_signature *sig = NULL;
    git_tree *tree = NULL;

    // initialize a new git repository in the given path
    int error = git_repository_init(&repo, Tcl_GetString(state_ptr->project_venv_dir_ptr), 0);
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        return TCL_ERROR;
    }

    fprintf(stdout, "initialized empty git repository in %s\n", Tcl_GetString(state_ptr->project_venv_dir_ptr));

    // Create a new index
    error = git_repository_index(&index, repo);
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        goto cleanup;
    }

    // Write the index as a tree
    error = git_index_write_tree(&tree_oid, index);
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        goto cleanup;
    }

    // Write the index to disk
    error = git_index_write(index);
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        goto cleanup;
    }

    // Create a tree from the tree OID
    error = git_tree_lookup(&tree, repo, &tree_oid);
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        goto cleanup;
    }

    // initial commit
    error = git_signature_now(&sig, "Author Name", "author@example.com");
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        goto cleanup;
    }

    // Create the initial commit
    git_oid commit_oid;
    error = git_commit_create_v(&commit_oid, repo, "HEAD", sig, sig, NULL, "Initial commit", tree, 0);
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        goto cleanup;
    }

    printf("Created initial commit with OID: %s\n", git_oid_tostr_s(&commit_oid));

    return TCL_OK;

    cleanup:
    // Clean up
    git_signature_free(sig);
    git_tree_free(tree);
    git_index_free(index);
    git_repository_free(repo);
    git_libgit2_shutdown();
    return TCL_ERROR;
}


int ttrek_GitResetHard(ttrek_state_t *state_ptr) {
    // git2
    git_libgit2_init();
    int error;
    git_repository *repo = NULL;
    git_commit *commit = NULL;
    const char *commit_sha = "HEAD";

    if (git_repository_open(&repo, Tcl_GetString(state_ptr->project_venv_dir_ptr)) != 0) {
        fprintf(stderr, "error: opening repository failed\n");
        git_libgit2_shutdown();
        return TCL_ERROR;
    }

    // Get the OID of the commit to reset to
    error = git_revparse_single((git_object **)&commit, repo, commit_sha);
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        git_repository_free(repo);
        git_libgit2_shutdown();
        return 1;
    }

    // Perform the hard reset
    error = git_reset(repo, (git_object *)commit, GIT_RESET_HARD, NULL);
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
    } else {
        printf("Hard reset to %s successful.\n", commit_sha);
    }

    // Clean up
    git_commit_free(commit);
    git_repository_free(repo);
    git_libgit2_shutdown();
    return TCL_OK;
}

int ttrek_GitCommit(ttrek_state_t *state_ptr, const char *message) {
    // git2
    git_libgit2_init();
    int error;
    git_repository *repo = NULL;
    git_index *index = NULL;
    git_oid tree_oid;
    git_oid commit_oid;
    git_signature *sig = NULL;
    git_tree *tree = NULL;
    git_commit *parent = NULL;

    if (git_repository_open(&repo, Tcl_GetString(state_ptr->project_venv_dir_ptr)) != 0) {
        fprintf(stderr, "error: opening repository failed\n");
        git_libgit2_shutdown();
        return TCL_ERROR;
    }

    // Get the index
    error = git_repository_index(&index, repo);
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        git_repository_free(repo);
        git_libgit2_shutdown();
        return TCL_ERROR;
    }

    // Add all files to the index (including modified and deleted)
    error = git_index_add_all(index, NULL, 0, NULL, NULL);
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        git_index_free(index);
        git_repository_free(repo);
        git_libgit2_shutdown();
        return TCL_ERROR;
    }

    // Write the index as a tree
    error = git_index_write_tree(&tree_oid, index);
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        git_index_free(index);
        git_repository_free(repo);
        git_libgit2_shutdown();
        return TCL_ERROR;
    }

    // Write the index to disk
    error = git_index_write(index);
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        git_index_free(index);
        git_repository_free(repo);
        git_libgit2_shutdown();
        return TCL_ERROR;
    }

    // Create a tree from the tree OID
    error = git_tree_lookup(&tree, repo, &tree_oid);
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        git_index_free(index);
        git_repository_free(repo);
        git_libgit2_shutdown();
        return TCL_ERROR;
    }

    // Get the parent commit
    error = git_reference_name_to_id(&commit_oid, repo, "HEAD");
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        git_tree_free(tree);
        git_index_free(index);
        git_repository_free(repo);
        git_libgit2_shutdown();
        return TCL_ERROR;
    }

    error = git_commit_lookup(&parent, repo, &commit_oid);
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        git_tree_free(tree);
        git_index_free(index);
        git_repository_free(repo);
        git_libgit2_shutdown();
        return TCL_ERROR;
    }

    // Create the commit
    error = git_signature_now(&sig, "Author Name", "author@example.com");
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        git_commit_free(parent);
        git_tree_free(tree);
        git_index_free(index);
        git_repository_free(repo);
        git_libgit2_shutdown();
        return TCL_ERROR;
    }

    error = git_commit_create_v(&commit_oid, repo, "HEAD", sig, sig, NULL, message, tree, 1, parent);
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %d/%d: %s\n", error, e->klass, e->message);
        git_signature_free(sig);
        git_commit_free(parent);
        git_tree_free(tree);
        git_index_free(index);
        git_repository_free(repo);
        git_libgit2_shutdown();
        return TCL_ERROR;
    }

    printf("Created commit with OID: %s\n", git_oid_tostr_s(&commit_oid));

    // Clean up
    git_signature_free(sig);
    git_commit_free(parent);
    git_tree_free(tree);
    git_index_free(index);
    git_repository_free(repo);
    git_libgit2_shutdown();
    return TCL_OK;
}
