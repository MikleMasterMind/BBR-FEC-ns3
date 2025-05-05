import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
import logging
from typing import Dict, Optional, Tuple, List
import sys

# Настройка логирования
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

def clean_data(df: pd.DataFrame) -> pd.DataFrame:
    """Обрабатывает дубликаты во временных метках, оставляя последнее значение"""
    return df.groupby('time', as_index=False).last()

def resample_to_grid(data: pd.DataFrame, grid: np.ndarray) -> pd.DataFrame:
    """
    Переносит данные на регулярную сетку с сохранением последнего известного значения
    
    Параметры:
        data - DataFrame с колонками ['time', 'value']
        grid - массив с временными метками сетки
        
    Возвращает:
        DataFrame с данными на регулярной сетке
    """
    if data.empty:
        return pd.DataFrame({'time': grid, 'value': np.nan})
    
    # Удаляем дубликаты временных меток
    clean_df = clean_data(data)
    
    # Создаем Series с уникальными временными метками
    original_series = pd.Series(clean_df['value'].values, index=clean_df['time'].values)
    
    # Ресемплируем на новую сетку с заполнением предыдущими значениями
    resampled = original_series.reindex(grid, method='ffill')
    
    # Заполняем оставшиеся NaN первым известным значением
    if resampled.isna().any() and not original_series.empty:
        resampled = resampled.fillna(original_series.iloc[0])
    
    return pd.DataFrame({'time': grid, 'value': resampled.values})

def process_all_files(data_dir: Path, grid: np.ndarray) -> Dict[str, pd.DataFrame]:
    """
    Обрабатывает все файлы данных и переносит их на регулярную сетку
    
    Параметры:
        data_dir - директория с файлами данных
        grid - временная сетка
        
    Возвращает:
        Словарь с обработанными DataFrame для каждого файла
    """
    file_patterns = {
        'cwnd': 'cwnd.data',
        'ssth': 'ssth.data',
        'rtt': 'rtt.data',
        'rto': 'rto.data',
        'inflight': 'inflight.data',
        'throughput': 'throughput.data',
        'loss': 'loss.data',
        'delay': 'delay.data'
    }
    
    three_columns_files = {
        'delay',
        'loss',
        'throughput'
    }
    
    prepare_value_files = {
        'delay'
    }
    
    processed = {}
    
    for metric, filename in file_patterns.items():
        filepath = data_dir / filename
        if not filepath.exists():
            logger.warning(f"Файл {filename} не найден")
            continue
            
        try:
            if metric in three_columns_files:
                # Чтение данных с новым параметром
                df = pd.read_csv(filepath, sep='\s+', header=None, 
                                names=['time', 'flow_id', 'value'], engine='python')
                
                if metric in prepare_value_files:
                    df['value'] = df['value'].str.replace('ns', '').astype(float)
                
                # Перенос на сетку
                for flow_id, group in df.groupby('flow_id'):
                    processed[f'{metric}_{flow_id}'] = resample_to_grid(group[['time', 'value']], grid)
            else:
                # Чтение данных с новым параметром
                df = pd.read_csv(filepath, sep='\s+', header=None, 
                                names=['time', 'value'], engine='python')
            
                # Перенос на сетку
                processed[metric] = resample_to_grid(df, grid)
            
        except Exception as e:
            logger.error(f"Ошибка обработки {filename}: {str(e)}")
    
    return processed

def create_plots(processed_data: Dict[str, pd.DataFrame], output_dir: Path) -> None:
    """
    Создает графики из обработанных данных
    
    Параметры:
        processed_data - словарь с обработанными DataFrame
        output_dir - директория для сохранения графиков
    """
    plt.style.use('seaborn-v0_8')
    colors = plt.cm.tab10.colors
    
    for metric, df in processed_data.items():
        if df is None or df.empty:
            continue
            
        fig, ax = plt.subplots(figsize=(10, 5))
        
        ax.plot(df['time'], df['value'], 
                color=colors[list(processed_data.keys()).index(metric) % 10],
                linewidth=1.5, label=metric)
        
        ax.set_title(f"Метрика: {metric}", fontsize=12)
        ax.set_xlabel("Время (секунды)", fontsize=10)
        ax.set_ylabel(metric, fontsize=10)
        ax.grid(True, linestyle=':', alpha=0.7)
        ax.legend()
        
        plot_path = output_dir / f"{metric}.png"
        fig.savefig(plot_path, dpi=300, bbox_inches='tight')
        plt.close(fig)
        logger.info(f"Создан график: {plot_path}")
    

def main(path : str):
    # Создаем регулярную сетку: 0-10 сек с шагом 0.1 мс (0.0001 сек)
    time_grid = np.arange(0, 200.31, 0.001)
    
    # Путь к данным (замените на ваш)
    data_directory = Path(path)
    
    if not data_directory.exists():
        logger.error(f"Директория с данными не найдена: {data_directory}")
        return
    
    # Директория для графиков
    output_dir = data_directory / "plots"
    output_dir.mkdir(exist_ok=True)
    
    # Обработка данных
    processed_data = process_all_files(data_directory, time_grid)
    
    # Создание графиков
    create_plots(processed_data, output_dir)
    
    logger.info("Обработка завершена")

if __name__ == "__main__":
    mydir = sys.argv[1]
    main(mydir)