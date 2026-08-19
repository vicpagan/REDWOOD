#!/usr/bin/env python3

from __future__ import annotations

from io import BytesIO
from pathlib import Path

import pandas as pd
import streamlit as st

SEP = "__"

st.set_page_config(page_title="REDWOOD Results Explorer", layout="wide")
st.title("REDWOOD Results Explorer")
st.caption("Filter experiment configurations interactively and compare heuristic / hack combinations.")


def is_heuristic_column(name: str) -> bool:
    return name.count(SEP) >= 2


def parse_heuristic(name: str) -> dict[str, object]:
    out = {"family": None, "foresight": None, "direction": None, "comparator": None}
    if name in {"dynamic", "random"}:
        out["family"] = name
        return out

    parts = name.split("_")
    if len(parts) >= 3 and parts[0] == "static":
        out["family"] = "static"
        out["foresight"] = parts[1]
        out["comparator"] = "_".join(parts[2:])
    elif len(parts) >= 4 and parts[0] == "greedy":
        out["family"] = "greedy"
        out["foresight"] = parts[1]
        out["direction"] = parts[2]
        out["comparator"] = "_".join(parts[3:])
    else:
        out["family"] = parts[0] if parts else name
    return out


@st.cache_data(show_spinner="Normalizing results...")
def normalize(raw: pd.DataFrame) -> pd.DataFrame:
    result_cols = [c for c in raw.columns if is_heuristic_column(c)]
    if not result_cols:
        raise ValueError("No heuristic columns found. Expected names like dynamic__independent__variant")

    config_cols = [c for c in raw.columns if c not in result_cols]
    df = raw.melt(
        id_vars=config_cols,
        value_vars=result_cols,
        var_name="_configuration",
        value_name="result",
    )

    parsed = df["_configuration"].str.rsplit(SEP, n=2, expand=True)
    df["heuristic"] = parsed[0]
    df["temporal_redundancy"] = parsed[1]
    df["reactive_rescheduling"] = parsed[2]
    df.drop(columns="_configuration", inplace=True)
    df["result"] = pd.to_numeric(df["result"], errors="coerce")

    lookup = []
    for h in df["heuristic"].dropna().unique():
        lookup.append({"heuristic": h, **parse_heuristic(str(h))})
    df = df.merge(pd.DataFrame(lookup), on="heuristic", how="left", validate="many_to_one")

    if "max_error" in df.columns:
        max_error = pd.to_numeric(df["max_error"], errors="coerce")
        df["normalized_result"] = df["result"] / max_error
    else:
        df["normalized_result"] = pd.NA

    return df


@st.cache_data(show_spinner="Reading CSV...")
def read_path(path: str) -> pd.DataFrame:
    return pd.read_csv(Path(path).expanduser())


@st.cache_data(show_spinner="Reading uploaded CSV...")
def read_upload(data: bytes) -> pd.DataFrame:
    return pd.read_csv(BytesIO(data))


def unique_sorted(series: pd.Series) -> list:
    vals = series.dropna().unique().tolist()
    try:
        return sorted(vals)
    except TypeError:
        return sorted(vals, key=str)


def keep_selected(df: pd.DataFrame, col: str, selected: list) -> pd.DataFrame:
    if not selected:
        return df.iloc[0:0]
    return df[df[col].isin(selected)]


# ----------------------------- Data source -----------------------------
with st.sidebar:
    st.header("Data source")
    source = st.radio("Source", ["Local path", "Upload"], horizontal=True)

    raw = None
    if source == "Local path":
        csv_path = st.text_input("CSV path", "../experiment_results/results_export.csv")
        if csv_path:
            try:
                raw = read_path(csv_path)
            except Exception as exc:
                st.error(f"Could not read CSV: {exc}")
    else:
        uploaded = st.file_uploader("Upload CSV", type=["csv"])
        if uploaded is not None:
            try:
                raw = read_upload(uploaded.getvalue())
            except Exception as exc:
                st.error(f"Could not read CSV: {exc}")

if raw is None:
    st.info("Choose a CSV source in the sidebar.")
    st.stop()

try:
    data = normalize(raw)
except Exception as exc:
    st.error(str(exc))
    st.stop()


# ------------------------------- Filters -------------------------------
filtered = data

with st.sidebar:
    st.header("Filters")

    if "app_config_id" in filtered.columns:
        app_num = pd.to_numeric(filtered["app_config_id"], errors="coerce")
        valid = app_num.dropna()
        if not valid.empty:
            lo, hi = int(valid.min()), int(valid.max())
            if lo == hi:
                app_range = (lo, hi)
                st.write(f"app_config_id: {lo}")
            else:
                app_range = st.slider(
                    "app_config_id range",
                    min_value=lo,
                    max_value=hi,
                    value=(lo, hi),
                    step=1,
                )
            filtered = filtered[app_num.between(app_range[0], app_range[1])]

    if "repetition_id" in filtered.columns:
        repetition_num = pd.to_numeric(filtered["repetition_id"], errors="coerce")
        valid = repetition_num.dropna()
        if not valid.empty:
            lo, hi = int(valid.min()), int(valid.max())
            if lo == hi:
                repetition_range = (lo, hi)
                st.write(f"repetition_id: {lo}")
            else:
                repetition_range = st.slider(
                    "repetition_id range",
                    min_value=lo,
                    max_value=hi,
                    value=(lo, hi),
                    step=1,
                )

            filtered = filtered[
                repetition_num.between(
                    repetition_range[0],
                    repetition_range[1],
                )
            ]

    if "num_nodes" in filtered.columns:
        opts = unique_sorted(filtered["num_nodes"])
        selected = st.multiselect("Number of nodes", opts, default=opts)
        filtered = keep_selected(filtered, "num_nodes", selected)

    if "e_fail_multiplier" in filtered.columns:
        opts = unique_sorted(filtered["e_fail_multiplier"])
        selected = st.multiselect("e_fail_multiplier", opts, default=opts)
        filtered = keep_selected(filtered, "e_fail_multiplier", selected)

    st.divider()
    st.subheader("Heuristics")

    opts = unique_sorted(filtered["family"])
    selected = st.multiselect("Family", opts, default=opts)
    filtered = keep_selected(filtered, "family", selected)

    opts = unique_sorted(filtered["heuristic"])
    selected = st.multiselect("Heuristic", opts, default=opts)
    filtered = keep_selected(filtered, "heuristic", selected)

    comparator_opts = unique_sorted(filtered["comparator"])
    if comparator_opts:
        selected = st.multiselect("Comparator", comparator_opts, default=comparator_opts)
        filtered = filtered[
            filtered["comparator"].isna() | filtered["comparator"].isin(selected)
            ]

    foresight_opts = unique_sorted(filtered["foresight"])
    if foresight_opts:
        selected = st.multiselect("Foresight", foresight_opts, default=foresight_opts)
        filtered = filtered[
            filtered["foresight"].isna() | filtered["foresight"].isin(selected)
            ]

    st.divider()
    st.subheader("Hacks")

    opts = unique_sorted(filtered["temporal_redundancy"])
    selected = st.multiselect("Temporal redundancy", opts, default=opts)
    filtered = keep_selected(filtered, "temporal_redundancy", selected)

    opts = unique_sorted(filtered["reactive_rescheduling"])
    selected = st.multiselect("Reactive rescheduling", opts, default=opts)
    filtered = keep_selected(filtered, "reactive_rescheduling", selected)

    st.divider()
    st.subheader("Metric")
    metric_choice = st.radio(
        "Average",
        ["Raw result", "Result / max_error"],
        help=(
            "Raw result averages the error values directly. Result / max_error "
            "puts applications with different error scales on a relative scale."
        ),
    )

if filtered.empty:
    st.warning("The current filters produce no rows.")
    st.stop()

metric = "result" if metric_choice == "Raw result" else "normalized_result"
metric_label = "Average result" if metric == "result" else "Average result / max_error"


# ------------------------------ Top stats ------------------------------
metric_values = pd.to_numeric(filtered[metric], errors="coerce")
cols = st.columns(4)
cols[0].metric("Source rows", f"{len(raw):,}")
cols[1].metric("Filtered values", f"{len(filtered):,}")
cols[2].metric(
    "Applications",
    f"{filtered['app_config_id'].nunique():,}" if "app_config_id" in filtered.columns else "N/A",
)
cols[3].metric(metric_label, f"{metric_values.mean():.6g}")


# --------------------------- Comparison table --------------------------
st.header("Heuristic / hack comparison")
st.caption(
    "Rows are heuristic + hack combinations. Node counts are separate columns. "
    "Each cell is the mean across all currently included application configurations and repetitions."
)

group_cols = [
    "heuristic",
    "temporal_redundancy",
    "reactive_rescheduling",
    "num_nodes",
]
summary = (
    filtered.groupby(group_cols, dropna=False, observed=True, sort=False)[metric]
    .agg(mean="mean", std="std", count="count")
    .reset_index()
)

comparison = summary.pivot(
    index=["heuristic", "temporal_redundancy", "reactive_rescheduling"],
    columns="num_nodes",
    values="mean",
).reset_index()

comparison.columns = [
    f"{c} nodes" if isinstance(c, (int, float)) else c
    for c in comparison.columns
]

st.dataframe(comparison, width="stretch", hide_index=True)

with st.expander("Detailed statistics"):
    detailed = summary.rename(
        columns={
            "mean": metric_label,
            "std": "Standard deviation",
            "count": "Values averaged",
        }
    )
    st.dataframe(detailed, width="stretch", hide_index=True)


# ----------------------------- Trend chart -----------------------------
st.header("Node-count trend")
heuristics = unique_sorted(filtered["heuristic"])
plot_heuristic = st.selectbox("Heuristic to plot", heuristics)

plot_data = filtered[filtered["heuristic"] == plot_heuristic]
plot_summary = (
    plot_data.groupby(
        ["num_nodes", "temporal_redundancy", "reactive_rescheduling"],
        dropna=False,
        observed=True,
        sort=False,
    )[metric]
    .mean()
    .reset_index()
)
plot_summary["hack_configuration"] = (
        plot_summary["temporal_redundancy"].astype(str)
        + " / "
        + plot_summary["reactive_rescheduling"].astype(str)
)
plot_table = plot_summary.pivot(
    index="num_nodes",
    columns="hack_configuration",
    values=metric,
).sort_index()

st.line_chart(plot_table, x_label="Number of nodes", y_label=metric_label)


# -------------------------- Flexible comparison --------------------------
st.header("Compare settings")
st.caption(
    "Compare one experiment setting using the current sidebar filters. "
    "Lower average error is better. The overall table pools all selected "
    "heuristics; the second table shows the same comparison within each heuristic."
)

comparison_candidates = {
    "Temporal redundancy": "temporal_redundancy",
    "Reactive rescheduling": "reactive_rescheduling",
    "Heuristic": "heuristic",
    "Heuristic family": "family",
    "Foresight": "foresight",
    "Comparator": "comparator",
    "Number of nodes": "num_nodes",
    "e_fail_multiplier": "e_fail_multiplier",
}

comparison_candidates = {
    label: column
    for label, column in comparison_candidates.items()
    if column in filtered.columns
       and filtered[column].dropna().nunique() > 0
}

if comparison_candidates:
    comparison_label = st.selectbox(
        "Compare by",
        list(comparison_candidates.keys()),
        index=0,
    )
    comparison_col = comparison_candidates[comparison_label]

    compare_data = filtered[
        filtered[comparison_col].notna()
        & pd.to_numeric(filtered[metric], errors="coerce").notna()
        ].copy()

    if compare_data.empty:
        st.info("No non-null values are available for this comparison.")
    else:
        # ---- Overall comparison across all currently selected heuristics ----
        overall_compare = (
            compare_data.groupby(
                comparison_col,
                dropna=False,
                observed=True,
                sort=False,
            )[metric]
            .agg(mean="mean", std="std", count="count")
            .reset_index()
        )

        best_mean = overall_compare["mean"].min()
        overall_compare["Difference from best"] = (
                overall_compare["mean"] - best_mean
        )

        if best_mean != 0:
            overall_compare["% above best"] = (
                    overall_compare["Difference from best"] / abs(best_mean) * 100
            )
        else:
            overall_compare["% above best"] = pd.NA

        overall_compare = overall_compare.rename(
            columns={
                comparison_col: comparison_label,
                "mean": metric_label,
                "std": "Standard deviation",
                "count": "Values averaged",
            }
        ).sort_values(metric_label, ascending=True)

        st.subheader("Overall comparison")
        st.caption(
            "This averages across every heuristic and other setting still included "
            "by the sidebar filters."
        )
        st.dataframe(
            overall_compare,
            width="stretch",
            hide_index=True,
        )

        best_overall = overall_compare.iloc[0]
        st.metric(
            f"Best {comparison_label}",
            str(best_overall[comparison_label]),
            help=f"Lowest {metric_label.lower()} among the currently filtered data.",
        )

        chart_data = overall_compare.set_index(comparison_label)[metric_label]
        st.bar_chart(
            chart_data,
            x_label=comparison_label,
            y_label=metric_label,
        )

        # ---- Same comparison independently within every heuristic ----
        if comparison_col != "heuristic" and "heuristic" in compare_data.columns:
            st.subheader("Comparison within each heuristic")
            st.caption(
                f"Each row is one heuristic. Columns are the available "
                f"{comparison_label.lower()} values. Each cell is the mean using "
                "only that heuristic and the current sidebar filters."
            )

            within_heuristic = (
                compare_data.groupby(
                    ["heuristic", comparison_col],
                    dropna=False,
                    observed=True,
                    sort=False,
                )[metric]
                .agg(mean="mean", std="std", count="count")
                .reset_index()
            )

            heuristic_comparison = within_heuristic.pivot(
                index="heuristic",
                columns=comparison_col,
                values="mean",
            ).reset_index()

            st.dataframe(
                heuristic_comparison,
                width="stretch",
                hide_index=True,
            )

            # Find the lowest-error setting independently for each heuristic.
            best_rows = (
                within_heuristic.sort_values(
                    ["heuristic", "mean"],
                    ascending=[True, True],
                )
                .groupby("heuristic", as_index=False, sort=False)
                .first()
                .rename(
                    columns={
                        comparison_col: f"Best {comparison_label}",
                        "mean": metric_label,
                        "std": "Standard deviation",
                        "count": "Values averaged",
                    }
                )
            )

            st.subheader("Best setting for each heuristic")
            st.dataframe(
                best_rows[
                    [
                        "heuristic",
                        f"Best {comparison_label}",
                        metric_label,
                        "Standard deviation",
                        "Values averaged",
                    ]
                ],
                width="stretch",
                hide_index=True,
            )

            with st.expander("Detailed within-heuristic comparison"):
                detailed_compare = within_heuristic.rename(
                    columns={
                        comparison_col: comparison_label,
                        "mean": metric_label,
                        "std": "Standard deviation",
                        "count": "Values averaged",
                    }
                )
                st.dataframe(
                    detailed_compare,
                    width="stretch",
                    hide_index=True,
                )

else:
    st.info("No columns with comparable values are available in the current view.")


# ------------------------- App-level breakdown -------------------------
st.header("Application breakdown")
st.caption(
    "This prevents a single overall average from hiding large differences between application configurations."
)

breakdown_cols = [
    c
    for c in [
        "app_config_id",
        "num_nodes",
        "e_fail_multiplier",
        "heuristic",
        "temporal_redundancy",
        "reactive_rescheduling",
    ]
    if c in filtered.columns
]

app_breakdown = (
    filtered.groupby(breakdown_cols, dropna=False, observed=True, sort=False)[metric]
    .mean()
    .reset_index(name=metric_label)
)
st.dataframe(app_breakdown, width="stretch", hide_index=True)


# ------------------------------ Downloads ------------------------------
st.header("Download current view")
d1, d2 = st.columns(2)
d1.download_button(
    "Download comparison CSV",
    data=comparison.to_csv(index=False).encode("utf-8"),
    file_name="filtered_heuristic_comparison.csv",
    mime="text/csv",
)
d2.download_button(
    "Download detailed statistics CSV",
    data=detailed.to_csv(index=False).encode("utf-8"),
    file_name="filtered_heuristic_statistics.csv",
    mime="text/csv",
)