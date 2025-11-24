import json
import subprocess
import itertools
import os
from pathlib import Path
import pandas as pd
from datetime import datetime
import numpy as np

class ExperimentRunner:
    def __init__(self, base_config_path, output_dir="experiments"):
        """Initialize the experiment runner with a base configuration."""
        with open(base_config_path, 'r') as f:
            self.base_config = json.load(f)

        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)
        self.results = []

    def generate_config_variations(self, param_grid, num_configs=100, temporal_redundancy=False):
        """
        Generate ~num_configs configuration variations using random sampling.

        param_grid example:
        {
            'failures.lambda': (0.3, 1.0),  # (min, max) for uniform sampling - rounded to 2 decimals
            'execution.deadline': (3000, 20000),  # integers
            'application.initial_error_level': (0.1, 2.0)  # integers or rounded
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

                self._set_nested_value(config, key, value)
                params[key] = value

            # Set temporal redundancy
            self._set_nested_value(config, 'scheduling.hacks.temporal_redundancy', temporal_redundancy)
            params['scheduling.hacks.temporal_redundancy'] = temporal_redundancy

            configs.append((config, params, i))

        return configs

    def _set_nested_value(self, config, key_path, value):
        """Set a nested dictionary value using dot notation."""
        keys = key_path.split('.')
        current = config

        for key in keys[:-1]:
            current = current[key]

        current[keys[-1]] = value

    def run_experiment(self, config, params, config_id, seed, executor_path):
        """
        Run a single experiment with the given configuration and seed.

        executor_path: Path to your application, e.g., './redwood_sim'
        """
        # Set the seed in the config
        config['execution']['seed'] = seed

        # Create temporary config file
        config_file = self.output_dir / f"temp_config_{config_id}_{seed}.json"

        # Save configuration temporarily
        with open(config_file, 'w') as f:
            json.dump(config, f, indent=2)

        # Run the application
        try:
            result = subprocess.run(
                [executor_path, '--json', str(config_file)],
                capture_output=True,
                text=True,
                timeout=300  # 5 minute timeout
            )

            # Parse output
            output_data = self._parse_output(result.stdout)

            # Clean up temporary config file
            config_file.unlink()

            return {
                'config_id': config_id,
                'seed': seed,
                'params': params,
                'success': result.returncode == 0,
                'output': output_data
            }

        except subprocess.TimeoutExpired:
            config_file.unlink(missing_ok=True)
            return {
                'config_id': config_id,
                'seed': seed,
                'params': params,
                'success': False,
                'output': None,
                'error': 'Timeout'
            }
        except Exception as e:
            config_file.unlink(missing_ok=True)
            return {
                'config_id': config_id,
                'seed': seed,
                'params': params,
                'success': False,
                'output': None,
                'error': str(e)
            }

    def _parse_output(self, stdout):
        """
        Parse the output from redwood_sim application.

        Expected format:
        Total repeats: 100
        Num successes: 53
        Success rate: 0.53
        Avg error level: 14.4933
        """
        data = {}
        lines = stdout.strip().split('\n')

        for line in lines:
            if ':' in line:
                key, value = line.split(':', 1)
                key = key.strip().lower().replace(' ', '_')
                value = value.strip()

                try:
                    # Try to convert to float
                    data[key] = float(value)
                except ValueError:
                    # Keep as string if not a number
                    data[key] = value

        return data

    def run_all_experiments(self, param_grid, executor_path, num_configs=100, num_repeats=1000):
        """Run all experiment configurations with multiple seeds, for both temporal redundancy settings."""

        # Run experiments with temporal_redundancy = False
        print(f"\n{'='*80}")
        print(f"RUNNING EXPERIMENTS WITH TEMPORAL REDUNDANCY = FALSE")
        print(f"{'='*80}")
        configs_false = self.generate_config_variations(param_grid, num_configs, temporal_redundancy=False)
        results_false = self._run_config_set(configs_false, executor_path, num_repeats, "FALSE")

        # Run experiments with temporal_redundancy = True (same configs, different flag)
        print(f"\n{'='*80}")
        print(f"RUNNING EXPERIMENTS WITH TEMPORAL REDUNDANCY = TRUE")
        print(f"{'='*80}")
        configs_true = self.generate_config_variations(param_grid, num_configs, temporal_redundancy=True)
        results_true = self._run_config_set(configs_true, executor_path, num_repeats, "TRUE")

        # Combine results
        self.results = results_false + results_true

        # Save and analyze results separately
        self._save_and_analyze_results(results_false, suffix="temporal_false")
        self._save_and_analyze_results(results_true, suffix="temporal_true")
        self._save_and_analyze_results(self.results, suffix="combined")

        return self.results

    def _run_config_set(self, configs, executor_path, num_repeats, label):
        """Run a set of configurations and return results."""
        results = []

        print(f"Running {len(configs)} different configurations, each with {num_repeats} different seeds...")
        print(f"Total experiments: {len(configs) * num_repeats}")

        for config_idx, (config, params, config_id) in enumerate(configs):
            print(f"\n{'-'*60}")
            print(f"Configuration {config_idx + 1}/{len(configs)} (Temporal={label})")
            print(f"Parameters: {params}")
            print(f"{'-'*60}")

            for repeat_idx in range(num_repeats):
                seed = config_id * 10000 + repeat_idx  # Unique seed for each run

                if (repeat_idx + 1) % 5 == 0:
                    print(f"  Repeat {repeat_idx + 1}/{num_repeats}...", end='\r', flush=True)

                if (repeat_idx + 1) % 100 == 0:
                    print(f"  Repeat {repeat_idx + 1}/{num_repeats}...", end=' ')

                result = self.run_experiment(config, params, config_id, seed, executor_path)
                result['temporal_redundancy'] = params['scheduling.hacks.temporal_redundancy']
                results.append(result)

                if not result['success']:
                    print(f"\n  ✗ Failed at repeat {repeat_idx + 1}: {result.get('error', 'Unknown error')}")

            # Quick summary for this config
            config_results = [r for r in results if r['config_id'] == config_id and r['success']]
            if config_results:
                success_rates = [r['output'].get('success_rate', 0) for r in config_results if r['output']]
                avg_errors = [r['output'].get('avg_error_level', 0) for r in config_results if r['output']]
                if success_rates:
                    print(f"  Mean success rate: {np.mean(success_rates):.4f} ± {np.std(success_rates):.4f}")
                if avg_errors:
                    print(f"  Mean error level: {np.mean(avg_errors):.4f} ± {np.std(avg_errors):.4f}")

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


# Example usage
if __name__ == "__main__":
    # Initialize the experiment runner
    runner = ExperimentRunner(
        base_config_path="data/sample_input.json",
        output_dir="experiments"
    )

    # Define parameter ranges for sampling (exclude temporal_redundancy from param_grid)
    param_grid = {
        'failures.lambda': (0.3, 1.0),  # Will be rounded to 2 decimal places
        'execution.deadline': (3000, 20000),  # Will be rounded to whole number
        'application.initial_error_level': (0.1, 2.0)  # Will be rounded to whole number
    }

    # Run 100 different configurations, each repeated 1000 times with different seeds
    # This will run TWICE: once with temporal_redundancy=False, once with temporal_redundancy=True
    results = runner.run_all_experiments(
        param_grid=param_grid,
        executor_path='./build/redwood_sim',
        num_configs=100,
        num_repeats=1000
    )