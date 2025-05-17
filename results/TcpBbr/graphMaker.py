import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
import logging
from typing import Dict, Optional, Tuple, List
import sys
import textwrap

# Настройка логирования
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

def read_config_file(data_dir: Path) -> str:
    """
    Читает содержимое файла config.txt
    
    Параметры:
        data_dir - директория с файлами данных
        
    Возвращает:
        Содержимое файла config.txt или сообщение об его отсутствии
    """
    config_path = data_dir / "config.txt"
    if config_path.exists():
        try:
            with open(config_path, 'r') as f:
                return f.read()
        except Exception as e:
            logger.warning(f"Не удалось прочитать config.txt: {str(e)}")
            return "Не удалось прочитать config.txt"
    else:
        logger.warning("Файл config.txt не найден")
        return "Файл config.txt не найден"

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

def create_plots(processed_data: Dict[str, pd.DataFrame], output_dir: Path, config_text: str) -> None:
    """
    Создает графики из обработанных данных с текстом конфигурации слева
    
    Параметры:
        processed_data - словарь с обработанными DataFrame
        output_dir - директория для сохранения графиков
        config_text - текст из файла config.txt для добавления на графики
    """
    plt.style.use('seaborn-v0_8')
    colors = plt.cm.tab10.colors
    
    # Разделяем текст на строки с сохранением оригинальных переносов
    config_lines = config_text.split('\n')
    
    # Форматируем каждую строку отдельно (перенос длинных строк)
    formatted_lines = []
    for line in config_lines:
        formatted_lines.extend(textwrap.wrap(line, width=40))
    
    # Объединяем строки с сохранением оригинальных пустых строк
    wrapped_text = '\n'.join(formatted_lines)
    
    for metric, df in processed_data.items():
        if df is None or df.empty:
            continue
            
        # Создаем фигуру с двумя областями
        fig = plt.figure(figsize=(16, 6))
        
        # Область для текста конфига (15% ширины)
        ax_text = fig.add_axes([0.05, 0.05, 0.15, 0.9])
        ax_text.axis('off')
        
        # Добавляем текст с сохранением переносов
        ax_text.text(0, 1, wrapped_text, 
                   fontsize=8, 
                   va='top',
                   ha='left',
                   fontfamily='monospace',  # Используем моноширинный шрифт
                   bbox={'facecolor':'white', 'alpha':0.7, 'pad':5})
        
        # Область для графика (80% ширины)
        ax_plot = fig.add_axes([0.25, 0.1, 0.7, 0.85])
        
        # Построение графика
        ax_plot.plot(df['time'], df['value'], 
                    color=colors[list(processed_data.keys()).index(metric) % 10],
                    linewidth=1.5, label=metric)
        
        ax_plot.set_title(f"Метрика: {metric}", fontsize=12)
        ax_plot.set_xlabel("Время (секунды)", fontsize=10)
        ax_plot.set_ylabel(metric, fontsize=10)
        ax_plot.grid(True, linestyle=':', alpha=0.7)
        ax_plot.legend()
        
        # Сохранение графика
        plot_path = output_dir / f"{metric}.png"
        fig.savefig(plot_path, dpi=300, bbox_inches='tight')
        plt.close(fig)
        logger.info(f"Создан график: {plot_path}")

def main(path: str):
    # Создаем регулярную сетку: 0-200 сек с шагом 1 мс (0.001 сек)
    time_grid = np.arange(0, 200.31, 0.001)
    
    # Путь к данным
    data_directory = Path(path)
    
    if not data_directory.exists():
        logger.error(f"Директория с данными не найдена: {data_directory}")
        return
    
    # Читаем файл конфигурации
    config_text = read_config_file(data_directory)
    
    # Директория для графиков
    output_dir = data_directory / "plots"
    output_dir.mkdir(exist_ok=True)
    
    # Обработка данных
    processed_data = process_all_files(data_directory, time_grid)
    
    # Создание графиков с добавлением конфига
    create_plots(processed_data, output_dir, config_text)
    
    logger.info("Обработка завершена")

if __name__ == "__main__":
    # if len(sys.argv) < 2:
    #     print("Использование: python script.py <путь_к_данным>")
    #     sys.exit(1)
    with open('tmp_dirs.txt', 'r') as file:
        done_dirs = [l.strip() for l in file.readlines()]
    try:
        while dir := input():
            if dir not in done_dirs:
                main(dir)
                done_dirs.append(dir)
    except EOFError:
        pass
    with open('tmp_dirs.txt', 'w') as file:
        for dir in done_dirs:
            print(dir, file=file)