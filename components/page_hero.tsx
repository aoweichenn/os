interface PageHeroProperties {
  index: string;
  eyebrow: string;
  title: string;
  description: string;
}

export function PageHero({
  index,
  eyebrow,
  title,
  description,
}: PageHeroProperties) {
  return (
    <section className="pageHero">
      <div className="pageHeroGrid">
        <div>
          <span className="pageKicker">
            {index} / {eyebrow}
          </span>
          <h1>{title}</h1>
        </div>
        <p className="pageLead">{description}</p>
      </div>
    </section>
  );
}
