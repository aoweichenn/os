LATEX_SOURCE_DIR := source/latex

.PHONY: check pdf clean

check:
	python3 $(LATEX_SOURCE_DIR)/scripts/check_inputs.py
	@command -v xelatex >/dev/null 2>&1 || { echo "missing xelatex"; exit 1; }
	@command -v latexmk >/dev/null 2>&1 || { echo "missing latexmk"; exit 1; }

pdf: check
	cd $(LATEX_SOURCE_DIR) && latexmk -xelatex -interaction=nonstopmode -halt-on-error main.tex

clean:
	cd $(LATEX_SOURCE_DIR) && latexmk -C
