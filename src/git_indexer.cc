#include <gflags/gflags.h>
#include <sstream>

#include "codesearch.h"
#include "git_indexer.h"
#include "smart_git.h"

using namespace std;

DEFINE_string(order_root, "", "Walk top-level directories in this order.");
DEFINE_bool(revparse, false, "Display parsed revisions, rather than as-provided");

git_indexer::git_indexer(code_searcher *cs,
                         const string& repopath,
                         const string& name)
    : cs_(cs), repo_(0), name_(name) {
    git_repository_open(&repo_, repopath.c_str());
    assert(repo_);
}

git_indexer::~git_indexer() {
    git_repository_free(repo_);
}

void git_indexer::walk(const string& ref) {
    smart_object<git_commit> commit;
    smart_object<git_tree> tree;
    git_revparse_single(commit, repo_, (ref + "^0").c_str());
    git_commit_tree(tree, commit);

    char oidstr[GIT_OID_HEXSZ+1];
    string name = FLAGS_revparse ?
        strdup(git_oid_tostr(oidstr, sizeof(oidstr), git_commit_id(commit))) : ref;
    if (name_.size())
        name = name_ + ":" + name;

    walk_root(name, tree);
}

void git_indexer::walk_root(const string& ref, git_tree *tree) {
    map<string, const git_tree_entry *> root;
    vector<const git_tree_entry *> ordered;
    int entries = git_tree_entrycount(tree);
    for (int i = 0; i < entries; ++i) {
        const git_tree_entry *ent = git_tree_entry_byindex(tree, i);
        root[git_tree_entry_name(ent)] = ent;
    }

    istringstream stream(FLAGS_order_root);
    string dir;
    while(stream >> dir) {
        map<string, const git_tree_entry *>::iterator it = root.find(dir);
        if (it == root.end())
            continue;
        ordered.push_back(it->second);
        root.erase(it);
    }
    for (map<string, const git_tree_entry *>::iterator it = root.begin();
         it != root.end(); ++it)
        ordered.push_back(it->second);
    for (vector<const git_tree_entry *>::iterator it = ordered.begin();
         it != ordered.end(); ++it) {
        smart_object<git_object> obj;
        git_tree_entry_to_object(obj, repo_, *it);
        string path = string(git_tree_entry_name(*it));

        if (git_tree_entry_type(*it) == GIT_OBJ_TREE) {
            walk_tree(ref, path + "/", obj);
        } else if (git_tree_entry_type(*it) == GIT_OBJ_BLOB) {
            const char *data = static_cast<const char*>(git_blob_rawcontent(obj));
            cs_->index_file(ref, path, StringPiece(data, git_blob_rawsize(obj)));
        }
    }
}

void git_indexer::walk_tree(const string& ref,
                            const string& pfx,
                            git_tree *tree) {
    string path;
    int entries = git_tree_entrycount(tree);
    int i;
    for (i = 0; i < entries; i++) {
        const git_tree_entry *ent = git_tree_entry_byindex(tree, i);
        path = pfx + git_tree_entry_name(ent);
        smart_object<git_object> obj;
        git_tree_entry_to_object(obj, repo_, ent);
        if (git_tree_entry_type(ent) == GIT_OBJ_TREE) {
            walk_tree(ref, path + "/", obj);
        } else if (git_tree_entry_type(ent) == GIT_OBJ_BLOB) {
            const char *data = static_cast<const char*>(git_blob_rawcontent(obj));
            cs_->index_file(ref, path, StringPiece(data, git_blob_rawsize(obj)));
        }
    }
}
