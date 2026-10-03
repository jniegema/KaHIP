import argparse
import itertools
import pathlib
import re
import subprocess
import tempfile


def grid(shape):
    vertices = list(itertools.product(*(range(n) for n in shape)))
    index = {vertex: i for i, vertex in enumerate(vertices)}
    adjacency = [set() for _ in vertices]
    for i, vertex in enumerate(vertices):
        for axis in range(len(shape)):
            for delta in (-1, 1):
                neighbor = list(vertex)
                neighbor[axis] += delta
                if tuple(neighbor) in index:
                    adjacency[i].add(index[tuple(neighbor)])
    return adjacency


def write_graph(path, adjacency):
    path.write_text(f"{len(adjacency)} {sum(map(len, adjacency)) // 2}\n" +
                    "".join(" ".join(str(v + 1) for v in sorted(a)) + "\n" for a in adjacency))


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def read_ordering(path, n):
    lines = path.read_text().splitlines()
    labels = [int(line.split()[1]) - 1 for line in lines[1:]]
    require(int(lines[0]) == n and sorted(labels) == list(range(n)), "ordering is not a permutation")
    return labels


def fill_count(adjacency, labels):
    # Eliminate vertices explicitly; independent of KaHIP's monotone-adjacency algorithm.
    adjacency = list(map(set, adjacency))
    fill = 0
    for vertex in sorted(range(len(labels)), key=labels.__getitem__):
        neighbors = list(adjacency[vertex])
        for i, left in enumerate(neighbors):
            adjacency[left].remove(vertex)
            for right in neighbors[i + 1:]:
                if right not in adjacency[left]:
                    fill += 1
                    adjacency[left].add(right)
                    adjacency[right].add(left)
        adjacency[vertex].clear()
    return fill


def run_ordering(binary, graph, adjacency, output, options):
    result = subprocess.run([binary, str(graph), "--output_filename=" + str(output), *options],
                            capture_output=True, text=True, timeout=90)
    require(result.returncode == 0, result.stdout + result.stderr)
    labels = read_ordering(output, len(adjacency))
    reported = re.search(r"Number of fill-edges: (\d+)", result.stdout)
    if reported:
        actual = fill_count(adjacency, labels)
        require(int(reported[1]) == actual, f"fill count differs: reported={reported[1]}, reference={actual}")
        print(f"vertices={len(labels)}, fill={actual}, options={options}")
    return output.read_bytes()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ordering", required=True)
    parser.add_argument("--separator", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="kahip-test-") as directory:
        root = pathlib.Path(directory)
        # Prove the permutation checker rejects a corrupted result.
        broken = root / "broken.ord"
        broken.write_text("2\n1 1\n2 1\n")
        try:
            read_ordering(broken, 2)
        except AssertionError:
            pass
        else:
            raise AssertionError("permutation checker accepted duplicate labels")
        require(fill_count([{1, 3}, {0, 2}, {1, 3}, {0, 2}], [0, 1, 2, 3]) == 1,
                "a four-cycle must introduce exactly one fill edge")
        for shape in ((16, 16), (6, 6, 6)):
            adjacency = grid(shape)
            graph = root / "input.graph"
            write_graph(graph, adjacency)
            for preset in ("fast", "eco"):
                options = ["--preconfiguration=" + preset, "--seed=42"]
                first = run_ordering(args.ordering, graph, adjacency, root / "first.ord", options)
                repeated = run_ordering(args.ordering, graph, adjacency, root / "repeated.ord", options)
                require(first == repeated, "repeated default orderings differ")
                output = root / "separator.part"
                result = subprocess.run([args.separator, str(graph), "--output_filename=" + str(output), *options],
                                        capture_output=True, text=True, timeout=90)
                require(result.returncode == 0, result.stdout + result.stderr)
                parts = list(map(int, output.read_text().split()))
                require(len(parts) == len(adjacency) and set(parts) <= {0, 1, 2}, "invalid separator labels")
                require(all({parts[u], parts[v]} != {0, 1} for u, neighbors in enumerate(adjacency) for v in neighbors),
                        "an edge crosses between the separated blocks")
                reported = re.search(r"separator size (\d+)", result.stdout)
                require(reported is not None and int(reported[1]) == parts.count(2), "separator weight differs")
                print(f"vertices={len(parts)}, separator={parts.count(2)}, preset={preset}")


if __name__ == "__main__":
    main()
