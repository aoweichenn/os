LATEX_SOURCE_DIR := source/latex
PROJECT_ROOT := ../..
SOURCE_METRICS_FILE := $(LATEX_SOURCE_DIR)/generated/source_metrics.tex

.PHONY: metrics source-check check pdf phone-export clean

metrics:
	python3 $(PROJECT_ROOT)/tools/os.py source-metrics --latex-output $(SOURCE_METRICS_FILE)

source-check: metrics
	python3 $(LATEX_SOURCE_DIR)/scripts/check_inputs.py

check: source-check
	@command -v xelatex >/dev/null 2>&1 || { echo "missing xelatex"; exit 1; }
	@command -v latexmk >/dev/null 2>&1 || { echo "missing latexmk"; exit 1; }

pdf: check
	cd $(LATEX_SOURCE_DIR) && latexmk -xelatex -interaction=nonstopmode -halt-on-error main.tex

phone-export: pdf
	python3 $(PROJECT_ROOT)/tools/os.py phone-book-export

clean:
	cd $(LATEX_SOURCE_DIR) && latexmk -C
