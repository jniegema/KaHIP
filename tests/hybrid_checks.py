import argparse
import pathlib
import subprocess
import tempfile
from ordering_checks import grid, require, run_ordering, write_graph


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ordering", required=True)
    parser.add_argument("--fast-ordering")
    parser.add_argument("--metis", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="kahip-hybrid-test-") as directory:
        root = pathlib.Path(directory)
        adjacency = grid((7, 7, 7))
        graph = root / "input.graph"
        write_graph(graph, adjacency)
        base = ["--preconfiguration=fast", "--seed=42", "--max_initial_ns_tries=4", "--sep_num_vert_stop=100"]
        variants = [["--sep_portfolio=4", "--sep_rating=-2"],
                    ["--sep_portfolio=6", "--sep_rating=-2", "--sep_stop_cycle"]]
        if args.metis:
            variants += [["--metis_depth=0", "--metis_split=130", "--metis_nseps=3"],
                         ["--metis_depth=1", "--metis_split=130", "--metis_nseps=3", "--sep_portfolio=4", "--sep_rating=-2"],
                         ["--metis_depth=1", "--sep_metis_candidate", "--sep_deeper_on_tie"]]
            normal = run_ordering(args.ordering, graph, adjacency, root / "normal.ord", base)
            large = run_ordering(args.ordering, graph, adjacency, root / "large.ord", base + ["--metis_above_degree=1073741824"])
            require(normal == large, "a large density threshold must not select METIS on a sparse graph")
        for variant in variants:
            outputs = [run_ordering(args.ordering, graph, adjacency, root / f"threads-{threads}.ord",
                                   base + variant + [f"--threads={threads}"]) for threads in (1, 2, 6)]
            require(len(set(outputs)) == 1, "thread counts changed the ordering")
        without_fill = run_ordering(args.ordering, graph, adjacency, root / "no-fill.ord", base + ["--no_fill_count"])
        with_fill = run_ordering(args.ordering, graph, adjacency, root / "fill.ord", base)
        require(without_fill == with_fill, "skipping the fill diagnostic changed the ordering")
        invalid = [["--threads=-1"], ["--metis_below=-1"], ["--metis_depth=-2"], ["--metis_split=-1"],
                   ["--imbalance=-1"], ["--imbalance=nan"], ["--imbalance=inf"], ["--threads=2", "--sep_faster_ns"],
                   ["--metis_above_degree=-1"], ["--metis_nseps=0"], ["--sep_rating=99"], ["--sep_portfolio=0"],
                   ["--dissection_rec_limit=0"], ["--sep_num_vert_stop=1"], ["--max_initial_ns_tries=0"],
                   ["--sep_portfolio=4"], ["--sep_rating=-2"], ["--metis_split=100"], ["--sep_stop_cycle"],
                   ["--threads=1", "--sep_portfolio=2", "--sep_stop_cycle", "--sep_num_vert_stop=2147483647"]]
        if not args.metis:
            invalid += [["--metis_depth=0"], ["--threads=1", "--sep_metis_candidate"]]
        for options in invalid:
            # A nonexistent graph proves the rejection happens before graph I/O.
            result = subprocess.run([args.ordering, str(root / "missing.graph"), "--preconfiguration=fast", *options],
                                    capture_output=True, text=True, timeout=10)
            require(result.returncode != 0 and result.stderr and "Error opening" not in result.stdout,
                    f"invalid options were not rejected before I/O: {options}: {result.stdout} {result.stderr}")
        if args.fast_ordering:
            for option in ("--threads=6", "--metis_nseps=3", "--sep_rating=0", "--no_fill_count"):
                result = subprocess.run([args.fast_ordering, str(graph), option], capture_output=True, text=True, timeout=10)
                require(result.returncode != 0 and "invalid option" in result.stderr.lower(),
                        f"fast_node_ordering accepted an unsupported option: {option}: {result.stderr}")
        print(f"deterministic variants={len(variants)}, invalid configurations rejected={len(invalid)}")


if __name__ == "__main__":
    main()
