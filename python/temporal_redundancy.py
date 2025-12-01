import json
import os
import subprocess
from datetime import datetime
from multiprocessing import Pool, cpu_count
from pathlib import Path

import numpy as np
import pandas as pd


class ExperimentRunner:
    def __init__(self, base_config_path, output_dir="experiments"):
        """Initialize the experiment runner with a base configuration."""
        with open(base_config_path, 'r') as f:
            self.base_config = json.load(f)

        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)
        self.results = []

        # Generate seeds once for all experiments
        self.seeds = None

    def generate_seeds(self, num_repeats):
        """Generate a fixed list of random seeds to use across all configurations."""
        np.random.seed(42)  # For reproducibility
        self.seeds = np.random.randint(1, 1000000000, size=num_repeats).tolist()
        return self.seeds

    def generate_config_variations(self, param_grid, num_configs=100, temporal_redundancy=False):
        """
        Generate ~num_configs configuration variations using random sampling.

        param_grid example:
        {
            'failures.lambda': (0.3, 1.0),  # (min, max) for uniform sampling - rounded to 2 decimals
            'execution.deadline': (3000, 20000),  # integers
            'task_time_factor': (0.5, 2.0)  # slope parameter for linear time function
        }
        """
        configs = []

        # Set seed for reproducibility
        np.random.seed(42)

        for i in range(num_configs):
            config = json.loads(json.dumps(self.base_config))  # Deep copy
            params = {}

            for key, value_range in param_grid.items():
                if isinstance(value_range, tuple) and len(value_range) == 2:
                    # Continuous range - sample uniformly
                    value = np.random.uniform(value_range[0], value_range[1])

                    # Apply rounding rules
                    if 'lambda' in key.lower():
                        # Lambda: round to 2 decimal places
                        value = round(float(value), 2)
                    elif 'slope' in key.lower() or 'intercept' in key.lower():
                        # Task function parameters: round to 2 decimal places
                        value = round(float(value), 2)
                    else:
                        # Other parameters: round to whole numbers
                        value = int(round(float(value)))
                elif isinstance(value_range, list):
                    # Discrete choices - sample randomly
                    value = np.random.choice(value_range)
                else:
                    value = value_range

                # Convert numpy types to native Python types for JSON serialization
                if hasattr(value, 'item'):
                    value = value.item()
                elif isinstance(value, (np.bool_, np.integer, np.floating)):
                    value = value.item()
                elif isinstance(value, np.ndarray):
                    value = value.tolist()

                # Handle special keys
                if key == 'task_time_factor':
                    # Set the slope for linear time function
                    self._set_task_function(config, slope=value)
                elif key == 'task_time_intercept':
                    # Set the intercept for linear time function
                    self._set_task_function(config, intercept=value)
                else:
                    self._set_nested_value(config, key, value)

                params[key] = value

            # Configure single host with single task
            self._configure_single_host_task(config)

            # Set temporal redundancy
            self._set_nested_value(config, 'scheduling.hacks.temporal_redundancy', temporal_redundancy)
            params['scheduling.hacks.temporal_redundancy'] = temporal_redundancy

            configs.append((config, params, i))

        return configs

    def _configure_single_host_task(self, config):
        """Configure the system to use a single task with 2 execution options."""
        # Ensure we have exactly 1 task with 2 execution options
        if 'application' in config and 'tasks' in config['application']:
            # Keep only the first task
            if len(config['application']['tasks']) > 1:
                config['application']['tasks'] = [config['application']['tasks'][0]]

            task = config['application']['tasks'][0]

            # Set error level to always start at 1
            config['application']['initial_error_level'] = 1.0

            # Don't modify initial_data_size - keep whatever is in base config

            # Configure 2 execution options for the task
            # Very minimal task times for high success rates
            task['execution_options'] = [
                {
                    "name": "option1",
                    "parallel_efficiency": 0.999,
                    "t_function": {
                        "type": "affine",
                        "comments": "a + b * x + c * y",
                        "parameters": {
                            "a": 0.0,    # No base time
                            "b": 3.0,    # Will be modified by task_time_factor
                            "c": 0.0
                        }
                    },
                    "d_function": {
                        "type": "affine",
                        "comments": "a + b * x + c * y",
                        "parameters": {
                            "a": 0.0,
                            "b": 2.0,
                            "c": 0.0
                        }
                    },
                    "e_function": {
                        "type": "quadratic",
                        "comments": "a + b * x + c * y + d * x^2 + e * y^2",
                        "parameters": {
                            "a": 0.0,
                            "b": 0.0,
                            "c": 6.0,  # Higher error growth
                            "d": 0.0,
                            "e": 0.0
                        }
                    }
                },
                {
                    "name": "option2",
                    "parallel_efficiency": 0.999,
                    "t_function": {
                        "type": "affine",
                        "comments": "a + b * x + c * y",
                        "parameters": {
                            "a": 0.0,    # No base time
                            "b": 5.0,    # Slower (1.67x option1)
                            "c": 0.0
                        }
                    },
                    "d_function": {
                        "type": "quadratic",
                        "comments": "a + b * x + c * y + d * x^2 + e * y^2",
                        "parameters": {
                            "a": 0.0,
                            "b": 2.0,
                            "c": 0.0,
                            "d": 0.0,
                            "e": 0.0
                        }
                    },
                    "e_function": {
                        "type": "affine",
                        "comments": "a + b * x + c * y",
                        "parameters": {
                            "a": 0.0,
                            "b": 0.0,
                            "c": 3.5  # Lower error growth
                        }
                    }
                }
            ]

    def _set_task_function(self, config, slope=None, intercept=None):
        """Set the linear function parameters for the task's t_function (time)."""
        task = config['application']['tasks'][0]

        # Update both execution options' t_function parameters
        if 'execution_options' in task:
            for option in task['execution_options']:
                if 't_function' in option and 'parameters' in option['t_function']:
                    # Update slope (b parameter) if provided
                    if slope is not None:
                        option['t_function']['parameters']['b'] = slope

                    # Update intercept (a parameter) if provided
                    if intercept is not None:
                        option['t_function']['parameters']['a'] = intercept

    def _set_nested_value(self, config, key_path, value):
        """Set a nested dictionary value using dot notation."""
        keys = key_path.split('.')
        current = config

        for key in keys[:-1]:
            current = current[key]

        current[keys[-1]] = value

    def run_all_experiments(self, param_grid, executor_path, num_configs=100, num_repeats=1000, num_workers=None):
        """Run all experiment configurations with multiple seeds, for both temporal redundancy settings.

        Args:
            param_grid: Dictionary of parameter ranges
            executor_path: Path to the executable
            num_configs: Number of different configurations to generate
            num_repeats: Number of times to repeat each configuration with different seeds
            num_workers: Number of parallel worker processes (defaults to CPU count)
        """
        if num_workers is None:
            num_workers = cpu_count()

        print(f"Using {num_workers} worker processes for parallel execution")

        # Generate seeds once for all experiments
        print(f"\nGenerating {num_repeats} random seeds for all experiments...")
        self.generate_seeds(num_repeats)
        print(f"Seeds: {self.seeds[:10]}... (showing first 10)")

        # Run experiments with temporal_redundancy = False
        print(f"\n{'='*80}")
        print(f"RUNNING EXPERIMENTS WITH TEMPORAL REDUNDANCY = FALSE")
        print(f"{'='*80}")
        configs_false = self.generate_config_variations(param_grid, num_configs, temporal_redundancy=False)
        results_false = self._run_config_set_parallel(configs_false, executor_path, num_repeats, "FALSE", num_workers)

        # Run experiments with temporal_redundancy = True (same configs, different flag)
        print(f"\n{'='*80}")
        print(f"RUNNING EXPERIMENTS WITH TEMPORAL REDUNDANCY = TRUE")
        print(f"{'='*80}")
        configs_true = self.generate_config_variations(param_grid, num_configs, temporal_redundancy=True)
        results_true = self._run_config_set_parallel(configs_true, executor_path, num_repeats, "TRUE", num_workers)

        # Combine results
        self.results = results_false + results_true

        # Save and analyze results separately
        self._save_and_analyze_results(results_false, suffix="temporal_false")
        self._save_and_analyze_results(results_true, suffix="temporal_true")
        self._save_and_analyze_results(self.results, suffix="combined")

        # Generate comparison analysis
        self._compare_temporal_redundancy(results_false, results_true)

        return self.results

    def _run_config_set_parallel(self, configs, executor_path, num_repeats, label, num_workers):
        """Run a set of configurations in parallel and return results."""

        print(f"Running {len(configs)} different configurations, each with {num_repeats} different seeds...")
        print(f"Total experiments: {len(configs) * num_repeats}")

        # Create all experiment tasks (one task per seed per config)
        tasks = []
        for config, params, config_id in configs:
            for seed_idx, seed in enumerate(self.seeds):
                # Create a copy of the config for this specific run
                run_config = json.loads(json.dumps(config))  # Deep copy

                # Set num_repeats to 1 (each JSON runs once)
                run_config['execution']['num_repeats'] = 1
                # Set the specific seed
                run_config['execution']['seed'] = seed
                # Also set failures seed
                run_config['failures']['seed'] = seed

                tasks.append({
                    'config': run_config,
                    'params': params,
                    'config_id': config_id,
                    'seed': seed,
                    'seed_idx': seed_idx,
                    'executor_path': executor_path,
                    'output_dir': str(self.output_dir)
                })

        # Run experiments in parallel with progress tracking
        print(f"Starting parallel execution with {num_workers} workers...")
        completed = 0
        results = []

        with Pool(processes=num_workers) as pool:
            # Use imap_unordered for progress tracking
            for result in pool.imap_unordered(_run_single_experiment_wrapper, tasks):
                results.append(result)
                completed += 1

                # Print progress every 100 completions
                if completed % 100 == 0:
                    success_so_far = sum(1 for r in results if r['success'])
                    print(f"Progress: {completed}/{len(tasks)} completed ({success_so_far} successful, {completed - success_so_far} failed)")

        print(f"\nParallel execution completed. Processing {len(results)} results...")

        # Add temporal redundancy flag to results
        for result in results:
            # Find the matching config to get temporal redundancy value
            matching_config = next((c for c in configs if c[2] == result['config_id']), None)
            if matching_config:
                result['temporal_redundancy'] = matching_config[1]['scheduling.hacks.temporal_redundancy']

        # Count successful experiments
        successful_count = sum(1 for r in results if r['success'])
        failed_count = len(results) - successful_count

        print(f"Completed: {successful_count} successful, {failed_count} failed")

        if failed_count > 0:
            print(f"\nShowing first 5 errors:")
            error_count = 0
            for r in results:
                if not r['success'] and error_count < 5:
                    error_msg = r.get('error', 'Unknown error')
                    stderr = r.get('stderr', '')
                    print(f"  Config {r['config_id']}, Seed {r['seed']}:")
                    print(f"    Error: {error_msg}")
                    if stderr:
                        print(f"    Stderr: {stderr[:200]}")  # First 200 chars
                    error_count += 1
                    if error_count >= 5:
                        break

        # Print summary statistics per configuration
        print(f"\n{'='*60}")
        print(f"CONFIGURATION SUMMARIES (Temporal={label})")
        print(f"{'='*60}")

        config_ids = sorted(set(c[2] for c in configs))
        for config_id in config_ids:
            matching_config = next((c for c in configs if c[2] == config_id), None)
            if not matching_config:
                continue

            params = matching_config[1]
            config_results = [r for r in results if r['config_id'] == config_id and r['success']]

            if config_results:
                success_rates = [r['output'].get('success_rate', 0) for r in config_results if r['output']]
                avg_errors = [r['output'].get('avg_error_level', 0) for r in config_results if r['output']]

                print(f"\nConfig {config_id + 1}: {params}")
                if success_rates:
                    print(f"  Success rate: {np.mean(success_rates):.4f} ± {np.std(success_rates):.4f}")
                if avg_errors:
                    print(f"  Error level: {np.mean(avg_errors):.4f} ± {np.std(avg_errors):.4f}")
                print(f"  Completed: {len(config_results)}/{num_repeats}")

        return results

    def _save_and_analyze_results(self, results=None, suffix=""):
        """Save experiment results and generate summary statistics."""
        if results is None:
            results = self.results

        # Save raw results
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        results_file = self.output_dir / f"results_{suffix}_{timestamp}.json"
        with open(results_file, 'w') as f:
            json.dump(results, f, indent=2)
        print(f"\n{'='*60}")
        print(f"Results saved to: {results_file}")

        # Convert to DataFrame for analysis
        data = []
        for result in results:
            if result['success'] and result['output']:
                row = {
                    'config_id': result['config_id'],
                    'seed': result['seed'],
                    'temporal_redundancy': result.get('temporal_redundancy', False)
                }
                row.update(result['params'])
                row.update(result['output'])
                data.append(row)

        df = pd.DataFrame(data)

        if df.empty:
            print("No successful experiments to analyze!")
            return

        # Calculate statistics per configuration
        metrics = ['success_rate', 'avg_error_level', 'num_successes', 'total_repeats']

        summary_stats = []
        for config_id in df['config_id'].unique():
            config_df = df[df['config_id'] == config_id]
            stats = {'config_id': config_id}

            # Add parameter values
            for param in [col for col in df.columns if col not in ['config_id', 'seed'] + metrics]:
                stats[param] = config_df[param].iloc[0]

            # Calculate mean and std for each metric
            for metric in metrics:
                if metric in config_df.columns:
                    stats[f'{metric}_mean'] = config_df[metric].mean()
                    stats[f'{metric}_std'] = config_df[metric].std()

            summary_stats.append(stats)

        summary_df = pd.DataFrame(summary_stats)

        # Save summary
        summary_file = self.output_dir / f"summary_{suffix}_{timestamp}.csv"
        summary_df.to_csv(summary_file, index=False)
        print(f"Summary statistics saved to: {summary_file}")

        # Print overall statistics
        print(f"\n{'='*60}")
        print(f"SUMMARY STATISTICS - {suffix.upper()}")
        print(f"{'='*60}")
        print(f"Total configurations: {len(summary_df)}")
        print(f"Total successful runs: {len(df)}")

        for metric in metrics:
            mean_col = f'{metric}_mean'
            std_col = f'{metric}_std'
            if mean_col in summary_df.columns:
                print(f"\n{metric.replace('_', ' ').title()}:")
                print(f"  Across all configs - Mean of means: {summary_df[mean_col].mean():.4f}")
                print(f"  Across all configs - Std of means: {summary_df[mean_col].std():.4f}")
                print(f"  Average within-config std: {summary_df[std_col].mean():.4f}")

    def _compare_temporal_redundancy(self, results_false, results_true):
        """Generate comparison analysis between temporal redundancy settings."""
        print(f"\n{'='*80}")
        print(f"TEMPORAL REDUNDANCY COMPARISON")
        print(f"{'='*80}")

        # Convert both result sets to DataFrames
        data_false = []
        for result in results_false:
            if result['success'] and result['output']:
                row = {'config_id': result['config_id'], 'seed': result['seed']}
                row.update(result['params'])
                row.update(result['output'])
                data_false.append(row)

        data_true = []
        for result in results_true:
            if result['success'] and result['output']:
                row = {'config_id': result['config_id'], 'seed': result['seed']}
                row.update(result['params'])
                row.update(result['output'])
                data_true.append(row)

        df_false = pd.DataFrame(data_false)
        df_true = pd.DataFrame(data_true)

        if df_false.empty or df_true.empty:
            print("Insufficient data for comparison!")
            return

        # Calculate aggregate statistics
        metrics = ['success_rate', 'avg_error_level', 'num_successes']

        print("\nOVERALL COMPARISON:")
        print(f"{'Metric':<25} {'False':<20} {'True':<20} {'Difference':<15}")
        print("-" * 80)

        for metric in metrics:
            if metric in df_false.columns and metric in df_true.columns:
                mean_false = df_false[metric].mean()
                std_false = df_false[metric].std()
                mean_true = df_true[metric].mean()
                std_true = df_true[metric].std()
                diff = mean_true - mean_false

                print(f"{metric:<25} {mean_false:>7.4f} ± {std_false:<7.4f} {mean_true:>7.4f} ± {std_true:<7.4f} {diff:>+7.4f}")

        # Per-configuration comparison
        print(f"\n{'='*80}")
        print("PER-CONFIGURATION COMPARISON (sorted by error improvement):")
        print(f"{'='*80}")

        comparisons = []
        for config_id in df_false['config_id'].unique():
            if config_id in df_true['config_id'].values:
                config_false = df_false[df_false['config_id'] == config_id]
                config_true = df_true[df_true['config_id'] == config_id]

                sr_false = config_false['success_rate'].mean()
                sr_true = config_true['success_rate'].mean()
                sr_improvement = sr_true - sr_false

                err_false = config_false['avg_error_level'].mean()
                err_true = config_true['avg_error_level'].mean()
                err_improvement = err_false - err_true  # Positive = improvement

                params = {k: v for k, v in config_false.iloc[0].items()
                          if k not in ['config_id', 'seed', 'success_rate', 'avg_error_level', 'num_successes', 'total_repeats']}

                comparisons.append({
                    'config_id': config_id,
                    'params': params,
                    'sr_false': sr_false,
                    'sr_true': sr_true,
                    'sr_improvement': sr_improvement,
                    'err_false': err_false,
                    'err_true': err_true,
                    'err_improvement': err_improvement
                })

        # Sort by error improvement
        comparisons.sort(key=lambda x: x['err_improvement'], reverse=True)

        # Print all configs
        for i, comp in enumerate(comparisons):
            print(f"\n{i+1}. Config {comp['config_id'] + 1}: {comp['params']}")
            print(f"   Success rate: {comp['sr_false']:.4f} → {comp['sr_true']:.4f} (Δ {comp['sr_improvement']:+.4f})")
            print(f"   Error level: {comp['err_false']:.4f} → {comp['err_true']:.4f} (Δ {comp['err_improvement']:+.4f})")

        # Save comparison to CSV
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        comparison_file = self.output_dir / f"temporal_comparison_{timestamp}.csv"
        pd.DataFrame(comparisons).to_csv(comparison_file, index=False)
        print(f"\nFull comparison saved to: {comparison_file}")


def _run_single_experiment_wrapper(task):
    """
    Wrapper function for running a single experiment in a multiprocessing context.
    This needs to be a module-level function for pickling.
    """
    config = task['config']
    params = task['params']
    config_id = task['config_id']
    seed = task['seed']
    seed_idx = task['seed_idx']
    executor_path = task['executor_path']
    output_dir = Path(task['output_dir'])

    # Create temporary config file with unique name (config_id + seed_idx + PID)
    config_file = output_dir / f"temp_config_{config_id}_{seed_idx}_{os.getpid()}.json"

    # Save configuration temporarily
    try:
        with open(config_file, 'w') as f:
            json.dump(config, f, indent=2)

        # Run the application
        result = subprocess.run(
            [executor_path, '--json', str(config_file)],
            capture_output=True,
            text=True,
            timeout=300  # 5 minute timeout
        )

        # Parse output
        output_data = _parse_output(result.stdout)

        # Clean up temporary config file
        config_file.unlink()

        return {
            'config_id': config_id,
            'seed': seed,
            'seed_idx': seed_idx,
            'params': params,
            'success': result.returncode == 0,
            'output': output_data,
            'stderr': result.stderr if result.returncode != 0 else None
        }

    except subprocess.TimeoutExpired:
        config_file.unlink(missing_ok=True)
        return {
            'config_id': config_id,
            'seed': seed,
            'seed_idx': seed_idx,
            'params': params,
            'success': False,
            'output': None,
            'error': 'Timeout',
            'stderr': None
        }
    except Exception as e:
        config_file.unlink(missing_ok=True)
        return {
            'config_id': config_id,
            'seed': seed,
            'seed_idx': seed_idx,
            'params': params,
            'success': False,
            'output': None,
            'error': str(e),
            'stderr': None
        }


def _parse_output(stdout):
    """
    Parse the output from redwood_sim application.
    Module-level function for multiprocessing.
    """
    data = {}
    lines = stdout.strip().split('\n')

    for line in lines:
        if ':' in line:
            key, value = line.split(':', 1)
            key = key.strip().lower().replace(' ', '_')
            value = value.strip()

            try:
                data[key] = float(value)
            except ValueError:
                data[key] = value

    return data


# Example usage
if __name__ == "__main__":
    # Initialize the experiment runner
    runner = ExperimentRunner(
        base_config_path="../data/sample_input.json",
        output_dir="../experiment_results"
    )

    # Define parameter ranges for sampling (exclude temporal_redundancy from param_grid)
    param_grid = {
        'failures.lambda': (0.4, 0.9),        # Moderate failure rates
        'execution.deadline': (3000, 8000),   # More reasonable deadlines
        'task_time_factor': (0.8, 2.5)        # Moderate task time variation
    }

    # Run n different configurations, each repeated m times with different seeds
    # This will run TWICE: once with temporal_redundancy=False, once with temporal_redundancy=True
    # Uses all available CPU cores for parallel execution
    results = runner.run_all_experiments(
        param_grid=param_grid,
        executor_path='../build/redwood_sim',
        num_configs=250,
        num_repeats=2500,
        num_workers=None  # None = use all CPU cores, or set to specific number like 4, 8, etc.
    )